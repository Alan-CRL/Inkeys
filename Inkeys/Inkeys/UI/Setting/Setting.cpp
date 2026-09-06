module;

#include "Setting.Wrap.h"

// ImFluent 使用 imgui_internal.h，升级 Dear ImGui 时必须同步验证固定快照。
#if IMGUI_VERSION_NUM != 19270
#error "Inkeys Setting requires Dear ImGui 1.92.7 (IMGUI_VERSION_NUM == 19270)"
#endif

#include "../../../IdtConfiguration.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtHistoricalDrawpad.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../IdtImage.h"
#include "../../../IdtMagnification.h"
#include "../../../IdtOther.h"
#include "../../../IdtPlug-in.h"
#include "../../../IdtRts.h"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include "Setting.SessionState.h"
#include "../../../SuperTop/IdtSuperTop.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")

module Inkeys.UI.Setting;

import Inkeys.UI.Bar;
import Inkeys.UI.Ppt;
import Inkeys.UI.RenderPipeline;
import Inkeys.Helper.Thread;
import Inkeys.Net.Update;
import Inkeys.Load;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Helper.CrashHandler;
import Inkeys.Other.Config;
import Inkeys.Window;

// 从 imgui_impl_win32.cpp 中前向声明消息处理器
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

using namespace std;

namespace
{
	using Inkeys::UI::RenderPipeline::FrameContext;
	using Inkeys::UI::RenderPipeline::FrameResult;

	atomic<bool> settingInitialized = false;
	atomic<bool> settingSessionShouldStop = false;
	atomic<bool> settingWindowActive = true;
	atomic<Inkeys::UI::Setting::BackdropMode> settingBackdropMode =
		Inkeys::UI::Setting::BackdropMode::Solid;
	atomic<Inkeys::UI::Setting::InteractiveWindowOperation>
		settingInteractiveOperation =
		Inkeys::UI::Setting::InteractiveWindowOperation::None;
	atomic<LRESULT> settingCaptionPressedHit = HTNOWHERE;
	mutex settingLifecycleMutex;
	// Win32 backend 会调用 SetCapture/ReleaseCapture/IME，可能同步重入同一 WndProc。
	recursive_mutex settingImguiMutex;
	mutex settingStateMutex;
	mutex settingTitleBarGeometryMutex;
	mutex settingDrainMutex;
	condition_variable settingDrainCondition;
	bool settingSessionDrained = true;
	mutex settingInitializeMutex;
	condition_variable settingInitializeCondition;
	bool settingInitializeCompleted = false;
	bool settingInitializeSucceeded = false;
	atomic<uint64_t> settingThemeSerial = 1;
	uint64_t settingConsumedThemeSerial = 0;
	FrameContext settingFrameContext;
	FrameResult settingFrameResult = FrameResult::Idle;
	uint64_t settingSessionEpoch = 0;
	Inkeys::UI::Setting::SessionState settingSessionState;
	Inkeys::UI::Setting::BusinessCompletionSnapshot settingLastBusinessCompletion;
	Inkeys::UI::Setting::TitleBarGeometry settingTitleBarGeometry;
	float settingTitleTextWidth = 0.0F;
	float settingVersionTextWidth = 0.0F;

	constexpr auto DwmwaLegacyMicaEffect =
		static_cast<DWMWINDOWATTRIBUTE>(1029);

	enum class SettingAccentState : int
	{
		Disabled = 0,
		AcrylicBlurBehind = 4,
	};

	struct SettingAccentPolicy
	{
		SettingAccentState state = SettingAccentState::Disabled;
		DWORD flags = 0;
		DWORD gradientColor = 0;
		DWORD animationId = 0;
	};

	struct SettingWindowCompositionAttributeData
	{
		int attribute = 19; // WCA_ACCENT_POLICY
		PVOID data = nullptr;
		SIZE_T size = 0;
	};

	using SetWindowCompositionAttributeProc = BOOL(WINAPI*)(
		HWND, SettingWindowCompositionAttributeData*);

	[[nodiscard]] SetWindowCompositionAttributeProc
		QuerySetWindowCompositionAttribute() noexcept
	{
		static const auto function =
			reinterpret_cast<SetWindowCompositionAttributeProc>(GetProcAddress(
				GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
		return function;
	}

	void SetSettingAcrylic(HWND hwnd, bool enabled) noexcept
	{
		const auto setCompositionAttribute =
			QuerySetWindowCompositionAttribute();
		if (!hwnd || !setCompositionAttribute) return;

		SettingAccentPolicy policy{};
		policy.state = enabled
			? SettingAccentState::AcrylicBlurBehind
			: SettingAccentState::Disabled;
		// GradientColor 使用 AABBGGRR；浅色主题采用对称 RGB tint。
		policy.gradientColor = enabled ? 0xCCF7F7F7U : 0U;
		SettingWindowCompositionAttributeData data{};
		data.data = &policy;
		data.size = sizeof(policy);
		(void)setCompositionAttribute(hwnd, &data);
	}

	void DisableSettingBackdrop(HWND hwnd) noexcept
	{
		if (!hwnd) return;
		const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
		(void)DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
			&backdrop, sizeof(backdrop));
		const BOOL legacyMica = FALSE;
		(void)DwmSetWindowAttribute(hwnd, DwmwaLegacyMicaEffect,
			&legacyMica, sizeof(legacyMica));
		const BOOL redirectionAlpha = FALSE;
		(void)DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA,
			&redirectionAlpha, sizeof(redirectionAlpha));
		SetSettingAcrylic(hwnd, false);
		const MARGINS margins{};
		(void)DwmExtendFrameIntoClientArea(hwnd, &margins);
	}

	[[nodiscard]] Inkeys::UI::Setting::BackdropMode
		ApplySettingBackdrop(HWND hwnd) noexcept
	{
		DisableSettingBackdrop(hwnd);
		BOOL compositionEnabled = FALSE;
		if (!hwnd || FAILED(DwmIsCompositionEnabled(&compositionEnabled))
			|| !compositionEnabled)
			return Inkeys::UI::Setting::BackdropMode::Solid;

		const MARGINS fullClientFrame{ -1, -1, -1, -1 };
		const BOOL redirectionAlpha = TRUE;
		const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
		if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
			&backdrop, sizeof(backdrop)))
			&& SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &fullClientFrame)))
		{
			(void)DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA,
				&redirectionAlpha, sizeof(redirectionAlpha));
			return Inkeys::UI::Setting::BackdropMode::Mica;
		}

		const DWM_SYSTEMBACKDROP_TYPE noBackdrop = DWMSBT_NONE;
		(void)DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
			&noBackdrop, sizeof(noBackdrop));
		const BOOL legacyMica = TRUE;
		if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DwmwaLegacyMicaEffect,
			&legacyMica, sizeof(legacyMica)))
			&& SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &fullClientFrame)))
		{
			(void)DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA,
				&redirectionAlpha, sizeof(redirectionAlpha));
			return Inkeys::UI::Setting::BackdropMode::Mica;
		}

		const BOOL disableLegacyMica = FALSE;
		(void)DwmSetWindowAttribute(hwnd, DwmwaLegacyMicaEffect,
			&disableLegacyMica, sizeof(disableLegacyMica));
		const MARGINS noMargins{};
		(void)DwmExtendFrameIntoClientArea(hwnd, &noMargins);
		const auto setCompositionAttribute =
			QuerySetWindowCompositionAttribute();
		if (setCompositionAttribute)
		{
			SettingAccentPolicy policy{};
			policy.state = SettingAccentState::AcrylicBlurBehind;
			policy.gradientColor = 0xCCF7F7F7U;
			SettingWindowCompositionAttributeData data{};
			data.data = &policy;
			data.size = sizeof(policy);
			if (setCompositionAttribute(hwnd, &data))
				return Inkeys::UI::Setting::BackdropMode::Acrylic;
		}

		DisableSettingBackdrop(hwnd);
		return Inkeys::UI::Setting::BackdropMode::Solid;
	}

	[[nodiscard]] UINT QuerySettingDpi(HWND hwnd) noexcept
	{
		using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
		static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
		const UINT dpi = getDpiForWindow && hwnd ? getDpiForWindow(hwnd) : 0;
		if (dpi) return dpi;
		HDC screen = GetDC(nullptr);
		const UINT fallback = screen
			? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX)) : USER_DEFAULT_SCREEN_DPI;
		if (screen) ReleaseDC(nullptr, screen);
		return fallback ? fallback : USER_DEFAULT_SCREEN_DPI;
	}

	[[nodiscard]] bool AdjustSettingWindowRectForDpi(
		RECT& rect, UINT dpi) noexcept
	{
		using AdjustWindowRectExForDpiProc = BOOL(WINAPI*)(
			LPRECT, DWORD, BOOL, DWORD, UINT);
		static const auto adjustWindowRectExForDpi =
			reinterpret_cast<AdjustWindowRectExForDpiProc>(GetProcAddress(
				GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
		if (adjustWindowRectExForDpi)
			return adjustWindowRectExForDpi(&rect,
				Inkeys::Window::SettingWindowStyle, FALSE,
				Inkeys::Window::SettingWindowExStyle, dpi) != FALSE;
		// Win7 回退使用系统 DPI 下的标准 frame 换算。
		return AdjustWindowRectEx(&rect, Inkeys::Window::SettingWindowStyle,
			FALSE, Inkeys::Window::SettingWindowExStyle) != FALSE;
	}

	void UpdateSettingScale(UINT dpi) noexcept
	{
		settingUserScale = Inkeys::UI::Setting::NormalizeUserScale(
			setlist.settingGlobalScale);
		settingSystemDpiScale = Inkeys::UI::Setting::DpiScale(dpi);
		settingGlobalScale = settingSystemDpiScale * settingUserScale;
	}

	[[nodiscard]] bool QueryAppsUseLightTheme() noexcept
	{
		DWORD value = 1;
		DWORD size = sizeof(value);
		RegGetValueW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
		return value != 0;
	}

	[[nodiscard]] ImFluentThemePreset QuerySystemTheme() noexcept
	{
		HIGHCONTRASTW contrast{ sizeof(contrast) };
		const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST,
			sizeof(contrast), &contrast, 0)
			&& (contrast.dwFlags & HCF_HIGHCONTRASTON);
		switch (Inkeys::UI::Setting::ResolveThemeMode(
			highContrast, QueryAppsUseLightTheme()))
		{
		case Inkeys::UI::Setting::ThemeMode::HighContrast:
			return ImFluentThemePreset_HighContrast;
		case Inkeys::UI::Setting::ThemeMode::Dark:
			return ImFluentThemePreset_Dark;
		default:
			return ImFluentThemePreset_Light;
		}
	}

	void ApplySystemTheme(HWND hwnd) noexcept
	{
		const ImFluentThemePreset preset = QuerySystemTheme();
		ImFluent::SetThemePreset(preset);
		// 最小客户区仍需完整容纳全部路由；宽度由响应式模式另行控制。
		ImFluent::GetStyle().NavItemHeight = 36.0F;
		Widgets::style.ApplyGlobal(12.0F);
		const auto backdropMode = ApplySettingBackdrop(hwnd);
		settingBackdropMode.store(backdropMode, memory_order_release);
		if (backdropMode != Inkeys::UI::Setting::BackdropMode::Solid)
		{
			// 只在 DWM 材质实际启用后放开根背景，失败回退仍保持不透明。
			ImFluent::GetStyle().Colors[ImFluentCol_SolidBgBase].w = 0.0F;
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.0F;
		}
		const BOOL darkFrame = preset == ImFluentThemePreset_Dark;
		if (hwnd)
			DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
				&darkFrame, sizeof(darkFrame));
	}

	[[nodiscard]] float QuerySettingCaptionButtonWidth(
		HWND /*hwnd*/, float titleBarHeight) noexcept
	{
		// 按当前 Fluent/Windows 视觉目标使用 46-DIP caption cell。
		// 这里固定标题栏几何比例，而不是用传统 SM_CXSIZE/SM_CYSIZE
		// 比值放大；后者在高 DPI 下会让自绘按钮明显过宽。
		const float scale = titleBarHeight > 0.0F
			? titleBarHeight / Inkeys::UI::Setting::TitleBarHeightDip
			: 1.0F;
		return Inkeys::UI::Setting::TitleBarCaptionButtonWidthDip * scale;
	}

	[[nodiscard]] Inkeys::UI::Setting::TitleBarGeometry
		ResolveSettingTitleBarGeometry(HWND hwnd,
			float titleTextWidth = -1.0F,
			float versionTextWidth = -1.0F) noexcept
	{
		RECT client{};
		const float clientWidth = hwnd && GetClientRect(hwnd, &client)
			? static_cast<float>(max(0L, client.right - client.left))
			: static_cast<float>(max(0, SettingWindowWidth));
		const float titleBarHeight = Inkeys::UI::Setting::TitleBarHeightDip
			* settingGlobalScale;
		lock_guard lock(settingTitleBarGeometryMutex);
		if (titleTextWidth >= 0.0F) settingTitleTextWidth = titleTextWidth;
		if (versionTextWidth >= 0.0F) settingVersionTextWidth = versionTextWidth;
		settingTitleBarGeometry = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			clientWidth, settingGlobalScale,
			QuerySettingCaptionButtonWidth(hwnd, titleBarHeight),
			settingTitleTextWidth, settingVersionTextWidth);
		return settingTitleBarGeometry;
	}

	[[nodiscard]] bool LoadSettingWindowIconTexture(HWND hwnd) noexcept
	{
		constexpr int iconTextureSize = 64;
		HICON icon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0));
		if (!icon) icon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
		if (!icon) icon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
		if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
		if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
		if (!icon) return false;

		BITMAPINFO bitmapInfo{};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = iconTextureSize;
		bitmapInfo.bmiHeader.biHeight = -iconTextureSize;
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;

		void* iconPixels = nullptr;
		HDC screenDc = GetDC(nullptr);
		HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
		HBITMAP bitmap = memoryDc
			? CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS,
				&iconPixels, nullptr, 0)
			: nullptr;
		bool loaded = false;
		if (bitmap && iconPixels)
		{
			const HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
			ZeroMemory(iconPixels, iconTextureSize * iconTextureSize * 4);
			// 统一以高分辨率缓存 HWND 图标，渲染时再按系统 DPI 缩放。
			const BOOL drawn = DrawIconEx(memoryDc, 0, 0, icon,
				iconTextureSize, iconTextureSize, 0, nullptr, DI_NORMAL);
			if (previousBitmap) SelectObject(memoryDc, previousBitmap);
			if (drawn)
			{
				auto* pixels = static_cast<unsigned char*>(iconPixels);
				for (int pixel = 0; pixel < iconTextureSize * iconTextureSize; ++pixel)
				{
					const int offset = pixel * 4;
					const unsigned int alpha = pixels[offset + 3];
					if (alpha == 0 || alpha == 255) continue;
					// DrawIconEx 输出预乘 alpha；ImGui DX11 混合前还原为直通道颜色。
					pixels[offset + 0] = static_cast<unsigned char>(
						min(255U, pixels[offset + 0] * 255U / alpha));
					pixels[offset + 1] = static_cast<unsigned char>(
						min(255U, pixels[offset + 1] * 255U / alpha));
					pixels[offset + 2] = static_cast<unsigned char>(
						min(255U, pixels[offset + 2] * 255U / alpha));
				}
				loaded = LoadTextureFromMemory(
					pixels,
					iconTextureSize, iconTextureSize, &TextureSettingSign[0]);
			}
		}
		if (bitmap) DeleteObject(bitmap);
		if (memoryDc) DeleteDC(memoryDc);
		if (screenDc) ReleaseDC(nullptr, screenDc);
		return loaded;
	}

	[[nodiscard]] LRESULT HitTestSettingClientTitleBar(
		HWND hwnd, LPARAM lParam) noexcept
	{
		const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		POINT clientPoint = point;
		if (!ScreenToClient(hwnd, &clientPoint)) return HTCLIENT;
		const auto geometry = ResolveSettingTitleBarGeometry(hwnd);
		const float x = static_cast<float>(clientPoint.x);
		const float y = static_cast<float>(clientPoint.y);
		if (geometry.close.Contains(x, y)) return HTCLOSE;
		if (geometry.maximize.Contains(x, y)) return HTMAXBUTTON;
		if (geometry.minimize.Contains(x, y)) return HTMINBUTTON;
		if (geometry.versionVisible && geometry.version.Contains(x, y))
			return HTCLIENT;
		if (geometry.icon.Contains(x, y)) return HTSYSMENU;

		// 除交互控件外，整条 32-DIP title bar 都明确交给 Win32 作为
		// HTCAPTION。拖窗、双击最大化、Aero Snap 等因此走系统的
		// non-client move loop，而不是依赖 ImGui/client mouse dragging。
		if (y >= 0.0F && y < geometry.height) return HTCAPTION;
		return HTCLIENT;
	}

	[[nodiscard]] ImRect ToImRect(
		const Inkeys::UI::Setting::LayoutRect& rect,
		const ImVec2& origin) noexcept
	{
		return { { origin.x + rect.left, origin.y + rect.top },
			{ origin.x + rect.right, origin.y + rect.bottom } };
	}

	[[nodiscard]] bool IsSettingChromeHovered(HWND hwnd,
		const Inkeys::UI::Setting::LayoutRect& rect) noexcept
	{
		POINT point{};
		return hwnd && GetCursorPos(&point) && ScreenToClient(hwnd, &point)
			&& rect.Contains(static_cast<float>(point.x),
				static_cast<float>(point.y));
	}

	[[nodiscard]] ImU32 SettingChromeColor(
		ImFluentCol color, float alpha = 1.0F) noexcept
	{
		ImVec4 resolved = ImGui::ColorConvertU32ToFloat4(Widgets::Color(color));
		resolved.w *= alpha;
		return ImGui::GetColorU32(resolved);
	}

	void RenderSettingCaptionButton(ImDrawList* drawList, HWND hwnd,
		const ImVec2& origin, const Inkeys::UI::Setting::LayoutRect& rect,
		const char* glyph, bool closeButton, bool active)
	{
		const ImRect bounds = ToImRect(rect, origin);
		const bool hovered = active && IsSettingChromeHovered(hwnd, rect);
		const bool held = hovered && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (hovered)
		{
			ImU32 fill = 0;
			if (closeButton)
			{
				// Windows 关闭按钮的 critical red 与普通 subtle hover 分开处理。
				fill = held ? IM_COL32(164, 38, 27, 255)
					: IM_COL32(196, 43, 28, 255);
			}
			else
			{
				fill = SettingChromeColor(held
					? ImFluentCol_SubtleFillTertiary
					: ImFluentCol_SubtleFillSecondary);
			}
			drawList->AddRectFilled(bounds.Min, bounds.Max, fill);
		}

		const ImU32 glyphColor = closeButton && hovered
			? IM_COL32(255, 255, 255, 255)
			: SettingChromeColor(ImFluentCol_TextPrimary,
				active ? 1.0F : 0.55F);

		// Chrome glyph 视觉尺寸独立于 Body typography。直接用当前合并了
		// Fluent Icons 的字体按 10 DIP 绘制，避免 E921/E922/E923/E8BB
		// 跟随 14-DIP Body 字号而显得过大。
		const float glyphFontSize = Inkeys::UI::Setting::TitleBarCaptionGlyphSizeDip
			* settingGlobalScale;
		const float currentFontSize = (std::max)(1.0F, ImGui::GetFontSize());
		ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
		const float glyphScale = glyphFontSize / currentFontSize;
		glyphSize.x *= glyphScale;
		glyphSize.y *= glyphScale;
		drawList->AddText(ImGui::GetFont(), glyphFontSize,
			{ bounds.Min.x + (bounds.GetWidth() - glyphSize.x) * 0.5F,
				bounds.Min.y + (bounds.GetHeight() - glyphSize.y) * 0.5F },
			glyphColor, glyph);
	}

	[[nodiscard]] bool IsSettingCaptionHit(LRESULT hit) noexcept
	{
		return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE;
	}

	void InvokeSettingCaptionCommand(HWND hwnd, LRESULT hit) noexcept
	{
		if (!hwnd) return;
		switch (hit)
		{
		case HTMINBUTTON:
			(void)PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
			break;
		case HTMAXBUTTON:
			(void)PostMessageW(hwnd, WM_SYSCOMMAND,
				IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
			break;
		case HTCLOSE:
			// Settings 的既有语义是关闭按钮隐藏窗口；SC_CLOSE 会在本
			// WndProc 中统一拦截到 Hide()，不会退出整个 Inkeys 进程。
			(void)PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
			break;
		default:
			break;
		}
	}

	enum class SettingBusinessKind
	{
		WriteSetting,
		WritePptSetting,
		WriteConfig,
		ShellExecute,
		Information,
		ConfirmRestart,
		Restart,
		Close,
		ShowWindow,
		HideWindow,
		ClearInstallerAndSetAutoUpdate,
		ClearInstallerAndSetChannel,
		ClearInstallerAndSetArchitecture,
		CreateShortcut,
		ConfigureDdb,
		RestartDdb,
		WriteDdb,
		SetStartup,
		StartAutomaticUpdate,
	};

	struct SettingBusinessCommand
	{
		SettingBusinessKind kind = SettingBusinessKind::WriteSetting;
		wstring text;
		wstring verb;
		wstring parameters;
		wstring directory;
		string value;
		string digest;
		string jsonPayload;
		string ddbCloseJsonPayload;
		string ddbOpenJsonPayload;
		shared_ptr<Inkeys::Config> configSnapshot;
		bool flag = false;
		bool secondaryFlag = false;
		int showCommand = SW_SHOW;
	};

	class SettingBusinessQueue
	{
	public:
		bool Start()
		{
			lock_guard lock(mutex_);
			if (worker_.joinable()) return true;
			stopping_ = false;
			try
			{
				worker_ = jthread([this](stop_token token) { Run(token); });
			}
			catch (...)
			{
				return false;
			}
			return true;
		}

		void Stop() noexcept
		{
			{
				lock_guard lock(mutex_);
				stopping_ = true;
			}
			condition_.notify_all();
			if (worker_.joinable())
			{
				worker_.join();
			}
			lock_guard lock(mutex_);
			commands_.clear();
		}

		void Enqueue(SettingBusinessCommand command)
		{
			{
				lock_guard lock(mutex_);
				if (stopping_ || !worker_.joinable()) return;
				// FIFO 节点完整拥有 payload，禁止借用帧内字符串或临时对象。
				commands_.push_back(std::move(command));
			}
			condition_.notify_one();
		}

	private:
		void Run(stop_token token) noexcept
		{
			for (;;)
			{
				SettingBusinessCommand command;
				{
					unique_lock lock(mutex_);
					condition_.wait(lock, token, [this]
						{ return stopping_ || !commands_.empty(); });
					// 停止生产后继续排空既有命令，避免退出时丢失最后一次配置写盘。
					if (commands_.empty() && (stopping_ || token.stop_requested())) break;
					if (commands_.empty()) continue;
					command = std::move(commands_.front());
					commands_.pop_front();
				}
				bool succeeded = false;
				try { succeeded = Execute(command); }
				catch (...) { succeeded = false; }
				{
					lock_guard stateLock(settingStateMutex);
					const auto nextSerial =
						settingSessionState.BusinessCompletion().serial + 1;
					// worker 只在此发布不可变完成快照，渲染线程负责消费。
					settingSessionState.PublishBusinessCompletion(nextSerial, succeeded);
				}
				Inkeys::UI::RenderPipeline::Request(
					Inkeys::UI::RenderPipeline::Client::Settings);
			}
		}

		static bool Execute(const SettingBusinessCommand& command)
		{
			switch (command.kind)
			{
			case SettingBusinessKind::WriteSetting:
				return WriteSettingJson(command.jsonPayload);
			case SettingBusinessKind::WritePptSetting:
				return WritePptComSettingJson(command.jsonPayload);
			case SettingBusinessKind::WriteConfig:
				return command.configSnapshot && command.configSnapshot->Write();
			case SettingBusinessKind::ShellExecute:
				return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
					command.verb.empty() ? nullptr : command.verb.c_str(),
					command.text.c_str(),
					command.parameters.empty() ? nullptr : command.parameters.c_str(),
					command.directory.empty() ? nullptr : command.directory.c_str(),
					command.showCommand)) > 32;
			case SettingBusinessKind::Information:
				return MessageBoxW(nullptr, command.text.c_str(),
					L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK) != 0;
			case SettingBusinessKind::ConfirmRestart:
				if (MessageBoxW(nullptr, command.text.c_str(),
					L"Inkeys Tips | 智绘教提示",
					MB_OKCANCEL | MB_SYSTEMMODAL) == IDOK)
					RestartProgram();
				return true;
			case SettingBusinessKind::Restart:
				RestartProgram();
				return true;
			case SettingBusinessKind::Close:
				CloseProgram();
				return true;
			case SettingBusinessKind::ShowWindow:
				return Inkeys::Window::GetService().Show(
					Inkeys::Window::WindowRole::Setting);
			case SettingBusinessKind::HideWindow:
				return Inkeys::Window::GetService().Hide(
					Inkeys::Window::WindowRole::Setting);
			case SettingBusinessKind::ClearInstallerAndSetAutoUpdate:
			case SettingBusinessKind::ClearInstallerAndSetChannel:
			case SettingBusinessKind::ClearInstallerAndSetArchitecture:
			{
				const auto installerDir = globalPath + L"installer";
				error_code ec;
				if (filesystem::exists(installerDir, ec)) filesystem::remove_all(installerDir, ec);
				if (!ec) filesystem::create_directory(installerDir, ec);
				if (ec) return false;
				return WriteSettingJson(command.jsonPayload);
			}
			case SettingBusinessKind::CreateShortcut:
				if (_waccess(command.text.c_str(), 0) == -1
					|| !shortcutAssistant.IsShortcutPointingToDirectory(
						command.text, command.directory))
					shortcutAssistant.CreateShortcut(command.text, command.directory);
				return true;
			case SettingBusinessKind::ConfigureDdb:
			{
				const wstring& executable = command.text;
				(void)WriteSettingJson(command.jsonPayload);
				if (command.flag)
				{
					error_code ec;
					filesystem::create_directories(command.directory, ec);
					if (ec) return false;
					bool extract = _waccess(executable.c_str(), 0) == -1;
					if (!extract && !command.digest.empty())
					{
						sha256wrapper wrapper;
						extract = wrapper.getHashFromFileW(executable) != command.digest;
						if (extract && isProcessRunning(executable.c_str()))
						{
							WriteDdbInteractionJson(command.ddbCloseJsonPayload);
							for (int i = 0; i < 20 && isProcessRunning(executable.c_str()); ++i)
								this_thread::sleep_for(chrono::milliseconds(500));
						}
					}
					if (extract)
						Inkeys::Load::ExtractResourceFile(executable.c_str(), L"EXE", MAKEINTRESOURCE(237));
					if (!isProcessRunning(executable.c_str()))
					{
						WriteDdbInteractionJson(command.ddbOpenJsonPayload);
						return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
							command.secondaryFlag ? L"runas" : nullptr, executable.c_str(),
							nullptr, nullptr, SW_SHOWNORMAL)) > 32;
					}
					return true;
				}
				WriteDdbInteractionJson(command.ddbCloseJsonPayload);
				SetStartupState(false, executable, L"$Inkeys_DesktopDrawpadBlocker");
				error_code ec;
				filesystem::remove(command.directory + L"\\start_up.signal", ec);
				return true;
			}
			case SettingBusinessKind::RestartDdb:
				if (!isProcessRunning(command.text.c_str())) return true;
				WriteDdbInteractionJson(command.ddbCloseJsonPayload);
				for (int i = 0; i < 25 && isProcessRunning(command.text.c_str()); ++i)
					this_thread::sleep_for(chrono::milliseconds(500));
				WriteDdbInteractionJson(command.ddbOpenJsonPayload);
				return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
					command.flag ? L"runas" : nullptr, command.text.c_str(),
					nullptr, nullptr, SW_SHOWNORMAL)) > 32;
			case SettingBusinessKind::WriteDdb:
				(void)WriteSettingJson(command.jsonPayload);
				return WriteDdbInteractionJson(command.ddbCloseJsonPayload);
			case SettingBusinessKind::SetStartup:
				return SetStartupState(command.flag, command.text,
					command.parameters);
			case SettingBusinessKind::StartAutomaticUpdate:
				thread(AutomaticUpdate).detach();
				return true;
			}
			return false;
		}

		mutex mutex_;
		condition_variable_any condition_;
		deque<SettingBusinessCommand> commands_;
		jthread worker_;
		bool stopping_ = true;
	};

	SettingBusinessQueue settingBusinessQueue;

	void QueueBusiness(SettingBusinessCommand command)
	{
		// 在生产者线程冻结配置内容，worker 只消费 owned payload 并执行 I/O。
		switch (command.kind)
		{
		case SettingBusinessKind::WriteSetting:
			command.jsonPayload = CaptureSettingJson();
			break;
		case SettingBusinessKind::ClearInstallerAndSetAutoUpdate:
		case SettingBusinessKind::ClearInstallerAndSetChannel:
		case SettingBusinessKind::ClearInstallerAndSetArchitecture:
			{
				unique_lock<shared_mutex> lock(setlistUpdateMutex);
				if (command.kind == SettingBusinessKind::ClearInstallerAndSetAutoUpdate)
					setlist.enableAutoUpdate = command.flag;
				else if (command.kind == SettingBusinessKind::ClearInstallerAndSetChannel)
					setlist.UpdateChannel = command.value;
				else
					setlist.updateArchitecture = command.value;
			}
			command.jsonPayload = CaptureSettingJson();
			break;
		case SettingBusinessKind::ConfigureDdb:
			ddbInteractionSetList.enable = command.flag;
			ddbInteractionSetList.runAsAdmin = command.secondaryFlag;
			ddbInteractionSetList.hostPath = command.parameters;
			command.jsonPayload = CaptureSettingJson();
			command.ddbCloseJsonPayload = CaptureDdbInteractionJson(true, true);
			command.ddbOpenJsonPayload = CaptureDdbInteractionJson(true, false);
			break;
		case SettingBusinessKind::RestartDdb:
			command.ddbCloseJsonPayload = CaptureDdbInteractionJson(true, true);
			command.ddbOpenJsonPayload = CaptureDdbInteractionJson(true, false);
			break;
		case SettingBusinessKind::WriteDdb:
			command.jsonPayload = CaptureSettingJson();
			command.ddbCloseJsonPayload = CaptureDdbInteractionJson(
				command.flag, command.secondaryFlag);
			break;
		case SettingBusinessKind::WritePptSetting:
			command.jsonPayload = CapturePptComSettingJson();
			break;
		case SettingBusinessKind::WriteConfig:
			command.configSnapshot = make_shared<Inkeys::Config>();
			*command.configSnapshot = Inkeys::config;
			break;
		default:
			break;
		}
		settingBusinessQueue.Enqueue(std::move(command));
	}

	void QueueWriteSetting()
	{
		QueueBusiness({ SettingBusinessKind::WriteSetting });
	}

	void QueuePptComWriteSetting()
	{
		QueueBusiness({ SettingBusinessKind::WritePptSetting });
	}

	void QueueConfigWrite()
	{
		QueueBusiness({ SettingBusinessKind::WriteConfig });
	}

	void QueueShellExecute(wstring target, wstring verb = {},
		wstring parameters = {}, wstring directory = {}, int showCommand = SW_SHOW)
	{
		SettingBusinessCommand command;
		command.kind = SettingBusinessKind::ShellExecute;
		command.text = std::move(target);
		command.verb = std::move(verb);
		command.parameters = std::move(parameters);
		command.directory = std::move(directory);
		command.showCommand = showCommand;
		QueueBusiness(std::move(command));
	}

	void QueueInformation(wstring message)
	{
		QueueBusiness({ SettingBusinessKind::Information, std::move(message) });
	}

	void QueueConfirmRestart(wstring message)
	{
		QueueBusiness({ SettingBusinessKind::ConfirmRestart, std::move(message) });
	}

	void QueueRestart()
	{
		QueueBusiness({ SettingBusinessKind::Restart });
	}

	void QueueClose()
	{
		QueueBusiness({ SettingBusinessKind::Close });
	}

	void QueueDdbWriteInteraction(bool change, bool close)
	{
		SettingBusinessCommand command;
		command.kind = SettingBusinessKind::WriteDdb;
		command.flag = change;
		command.secondaryFlag = close;
		QueueBusiness(std::move(command));
	}

	bool QueueSetStartup(bool enabled, wstring path, const wstring& name)
	{
		SettingBusinessCommand command;
		command.kind = SettingBusinessKind::SetStartup;
		command.flag = enabled;
		command.text = std::move(path);
		command.parameters = name;
		QueueBusiness(std::move(command));
		return true;
	}

	void QueueAutomaticUpdate()
	{
		QueueBusiness({ SettingBusinessKind::StartAutomaticUpdate });
	}

	HINSTANCE QueueShellExecuteCompat(HWND, LPCWSTR operation, LPCWSTR file,
		LPCWSTR parameters, LPCWSTR directory, INT showCommand)
	{
		QueueShellExecute(file ? file : L"", operation ? operation : L"",
			parameters ? parameters : L"", directory ? directory : L"", showCommand);
		return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(33));
	}

	struct SettingSessionCoroutine
	{
		struct promise_type
		{
			SettingSessionCoroutine get_return_object() noexcept
			{
				return SettingSessionCoroutine(
					coroutine_handle<promise_type>::from_promise(*this));
			}
			suspend_always initial_suspend() const noexcept { return {}; }
			suspend_always final_suspend() const noexcept { return {}; }
			void return_void() const noexcept {}
			void unhandled_exception() const noexcept { terminate(); }
		};

		SettingSessionCoroutine() = default;
		explicit SettingSessionCoroutine(coroutine_handle<promise_type> value) noexcept
			: handle(value) {}
		SettingSessionCoroutine(SettingSessionCoroutine&& other) noexcept
			: handle(exchange(other.handle, {})) {}
		SettingSessionCoroutine& operator=(SettingSessionCoroutine&& other) noexcept
		{
			if (this == &other) return *this;
			Reset();
			handle = exchange(other.handle, {});
			return *this;
		}
		~SettingSessionCoroutine() { Reset(); }
		SettingSessionCoroutine(const SettingSessionCoroutine&) = delete;
		SettingSessionCoroutine& operator=(const SettingSessionCoroutine&) = delete;

		void Resume()
		{
			if (handle && !handle.done()) handle.resume();
		}
		[[nodiscard]] bool Done() const noexcept { return !handle || handle.done(); }
		void Reset() noexcept
		{
			if (handle) handle.destroy();
			handle = {};
		}

		coroutine_handle<promise_type> handle{};
	};

	SettingSessionCoroutine settingSession;
}

static void SyncUi3BuiltInComponents()
{
	barUISet.barButtonSet.SyncLegacyExtensionButtons();
	barUISet.UpdateRendering();
}

// 软件构建信息
// signal1
struct
{
	wstring url;

	wstring repoUrl;
	wstring branch;
	wstring submitter;
	wstring buildTime;

	wstring buildOS;
	wstring buildOSVersion;
	wstring buildRunnerImageOS;
	wstring buildRunnerImageVersion;

	wstring msBuildVersion;
} settingCICD;
// signal1

// Win32 消息处理器
// 您可以阅读 io.WantCaptureMouse、io.WantCaptureKeyboard 标志，以了解 dear imgui 是否想使用您的输入。
// - 当 io.WantCaptureMouse 为 true 时，请勿将鼠标输入数据发送到主应用程序，或者清除/覆盖鼠标数据的副本。
// - 当 io.WantCaptureKeyboard 为 true 时，请勿将键盘输入数据发送到主应用程序，或者清除/覆盖键盘数据的副本。
// 通常，您可以始终将所有输入传递给 dear imgui，并根据这两个标志在应用程序中隐藏它们。
LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// 保留 WS_THICKFRAME 的窗口语义，但不让原生 sizing frame 占用客户区。
	switch (msg)
	{
	case WM_NCCALCSIZE:
	{
		// 最大化时把 non-client geometry 完全交还 Windows。
		//
		// Windows 自己负责：
		// - maximized frame
		// - work area
		// - taskbar
		// - multi-monitor
		// - DPI
		// - maximized 边缘裁切
		if (::IsZoomed(hWnd))
		{
			return ::DefWindowProcW(
				hWnd,
				msg,
				wParam,
				lParam);
		}

		// 普通窗口状态：
		// 自定义为四边统一 1px non-client frame。
		if (wParam && lParam)
		{
			auto* params =
				reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

			constexpr LONG kVisibleFrame = 1;

			params->rgrc[0].left += kVisibleFrame;
			params->rgrc[0].top += kVisibleFrame;
			params->rgrc[0].right -= kVisibleFrame;
			params->rgrc[0].bottom -= kVisibleFrame;

			return 0;
		}

		return ::DefWindowProcW(
			hWnd,
			msg,
			wParam,
			lParam);
	}

	case WM_NCPAINT:
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);

	case WM_NCHITTEST:
	{
		// 自己统一四边的 resize hit zone。
		// 视觉上的顶部 frame 只有 1px，但 hit zone 使用系统真实 sizing frame 厚度。
		RECT frame{};
		if (AdjustSettingWindowRectForDpi(frame, QuerySettingDpi(hWnd)))
		{
			const LONG resizeX = max<LONG>(1, -frame.left);
			const LONG resizeY = max<LONG>(1, -frame.top);

			RECT windowRect{};
			if (::GetWindowRect(hWnd, &windowRect))
			{
				const LONG x = GET_X_LPARAM(lParam);
				const LONG y = GET_Y_LPARAM(lParam);

				const bool onLeft =
					x >= windowRect.left &&
					x < windowRect.left + resizeX;

				const bool onRight =
					x < windowRect.right &&
					x >= windowRect.right - resizeX;

				const bool onTop =
					y >= windowRect.top &&
					y < windowRect.top + resizeY;

				const bool onBottom =
					y < windowRect.bottom &&
					y >= windowRect.bottom - resizeY;

				if (onTop && onLeft)       return HTTOPLEFT;
				if (onTop && onRight)      return HTTOPRIGHT;
				if (onBottom && onLeft)    return HTBOTTOMLEFT;
				if (onBottom && onRight)   return HTBOTTOMRIGHT;

				if (onTop)                 return HTTOP;
				if (onBottom)              return HTBOTTOM;
				if (onLeft)                return HTLEFT;
				if (onRight)               return HTRIGHT;
			}
		}

		const LRESULT nativeHit =
			::DefWindowProcW(hWnd, msg, wParam, lParam);

		if (nativeHit != HTCLIENT)
			return nativeHit;

		return HitTestSettingClientTitleBar(hWnd, lParam);
	}

	case WM_NCLBUTTONDOWN:
	{
		const LRESULT hit = static_cast<LRESULT>(wParam);
		const auto operation =
			Inkeys::UI::Setting::InteractiveOperationFromHitTest(hit);
		if (operation != Inkeys::UI::Setting::InteractiveWindowOperation::None)
			settingInteractiveOperation.store(operation, memory_order_release);
		if (IsSettingCaptionHit(hit))
		{
			settingCaptionPressedHit.store(hit, memory_order_release);
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Client::Settings);
			return 0;
		}
		const LRESULT result = ::DefWindowProcW(hWnd, msg, wParam, lParam);
		if (operation != Inkeys::UI::Setting::InteractiveWindowOperation::None
			&& settingInteractiveOperation.load(memory_order_acquire) == operation)
		{
			settingInteractiveOperation.store(
				Inkeys::UI::Setting::InteractiveWindowOperation::None,
				memory_order_release);
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Client::Settings);
		}
		return result;
	}
	case WM_NCLBUTTONUP:
	{
		const LRESULT pressed = settingCaptionPressedHit.exchange(
			HTNOWHERE, memory_order_acq_rel);
		const LRESULT released = HitTestSettingClientTitleBar(hWnd, lParam);
		if (IsSettingCaptionHit(pressed))
		{
			if (released == pressed) InvokeSettingCaptionCommand(hWnd, pressed);
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Client::Settings);
			return 0;
		}
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	}
	case WM_NCLBUTTONDBLCLK:
	case WM_NCRBUTTONDOWN:
	case WM_NCRBUTTONUP:
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	case WM_NCMOUSEMOVE:
		// 拖窗时不持续唤醒 D3D11；仅 caption button hover 需要刷新。
		if (IsSettingCaptionHit(static_cast<LRESULT>(wParam)))
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Client::Settings);
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	case WM_NCMOUSELEAVE:
		settingCaptionPressedHit.store(HTNOWHERE, memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	case WM_CANCELMODE:
	case WM_CAPTURECHANGED:
		settingCaptionPressedHit.store(HTNOWHERE, memory_order_release);
		break;
	case WM_NCACTIVATE:
		settingWindowActive.store(wParam != FALSE, memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	case WM_SYSCOMMAND:
	{
		const auto operation =
			Inkeys::UI::Setting::InteractiveOperationFromSystemCommand(wParam);
		if (operation != Inkeys::UI::Setting::InteractiveWindowOperation::None)
			settingInteractiveOperation.store(operation, memory_order_release);
		if ((wParam & 0xFFF0) == SC_CLOSE)
		{
			Inkeys::UI::Setting::Hide();
			return 0;
		}
		const LRESULT result = ::DefWindowProcW(hWnd, msg, wParam, lParam);
		if (operation != Inkeys::UI::Setting::InteractiveWindowOperation::None
			&& settingInteractiveOperation.load(memory_order_acquire) == operation)
		{
			settingInteractiveOperation.store(
				Inkeys::UI::Setting::InteractiveWindowOperation::None,
				memory_order_release);
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Client::Settings);
		}
		return result;
	}
	default:
		break;
	}

	if (Inkeys::UI::Setting::IsVisible())
	{
		// HWND 线程只更新 IO；context/backend/draw/present 仍由渲染线程拥有。
		lock_guard lock(settingImguiMutex);
		if (ImGui::GetCurrentContext()
			&& ImGui_ImplWin32_WndProcHandlerEx(
				hWnd, msg, wParam, lParam, ImGui::GetIO())
			// Alt+Space 必须继续交给 DefWindowProc 打开原生系统菜单。
			&& !(msg == WM_SYSKEYDOWN && wParam == VK_SPACE))
			return true;
	}

	switch (msg)
	{
	case WM_GETMINMAXINFO:
	{
		auto* minMaxInfo =
			reinterpret_cast<MINMAXINFO*>(lParam);

		if (!minMaxInfo)
			return 0;

		RECT minimumRect{
			0,
			0,
			Inkeys::UI::Setting::ScaleDip(
				Inkeys::UI::Setting::MinimumWidthDip,
				settingGlobalScale),
			Inkeys::UI::Setting::ScaleDip(
				Inkeys::UI::Setting::MinimumHeightDip,
				settingGlobalScale)
		};

		(void)AdjustSettingWindowRectForDpi(
			minimumRect,
			QuerySettingDpi(hWnd));

		minMaxInfo->ptMinTrackSize.x =
			minimumRect.right - minimumRect.left;

		minMaxInfo->ptMinTrackSize.y =
			minimumRect.bottom - minimumRect.top;

		return 0;
	}

	case WM_ENTERSIZEMOVE:
		// 未知入口默认按 Size 处理，优先保证 live resize 不停帧。
		if (settingInteractiveOperation.load(memory_order_acquire)
			== Inkeys::UI::Setting::InteractiveWindowOperation::None)
			settingInteractiveOperation.store(
				Inkeys::UI::Setting::InteractiveWindowOperation::Size,
				memory_order_release);
		return 0;
	case WM_EXITSIZEMOVE:
		settingInteractiveOperation.store(
			Inkeys::UI::Setting::InteractiveWindowOperation::None,
			memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		return 0;
	case WM_DPICHANGED:
	{
		UpdateSettingScale(LOWORD(wParam));
		{
			lock_guard stateLock(settingStateMutex);
			// DPI 变化只请求字体图集重建，普通窗口缩放不触碰字体资源。
			settingSessionState.QueueFontRebuild();
		}
		const auto* suggested = reinterpret_cast<const RECT*>(lParam);
		if (suggested)
		{
			SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return 0;
	}
	case WM_DWMCOMPOSITIONCHANGED:
	case WM_THEMECHANGED:
	case WM_SETTINGCHANGE:
		settingThemeSerial.fetch_add(1, memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		break;
	case WM_ACTIVATE:
		settingWindowActive.store(LOWORD(wParam) != WA_INACTIVE,
			memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		break;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		{
			SettingWindowWidth = static_cast<int>(LOWORD(lParam));
			SettingWindowHeight = static_cast<int>(HIWORD(lParam));
			lock_guard stateLock(settingStateMutex);
			settingSessionState.QueueResize(
				static_cast<UINT>(LOWORD(lParam)),
				static_cast<UINT>(HIWORD(lParam)));
		}
		// UI 线程只发布最新尺寸，ResizeBuffers 仍由渲染线程执行。
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		return 0;
	case WM_CLOSE:
		Inkeys::UI::Setting::Hide();
		return 0;
	case WM_DESTROY:
	{
		settingInteractiveOperation.store(
			Inkeys::UI::Setting::InteractiveWindowOperation::None,
			memory_order_release);
		// 防御其他流氓软件关闭我的窗口
		return 0;
	}

	case WM_MOVE:
		RECT rect;
		GetWindowRect(hWnd, &rect);

		SettingWindowX = rect.left;
		SettingWindowY = rect.top;

		break;
	}

	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void SettingWindowBegin()
{
	// 尺寸计算
	{
		UpdateSettingScale(QuerySettingDpi(nullptr));
		RECT workArea{ 0, 0, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight };
		(void)SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
		const int rawWorkWidth = static_cast<int>(workArea.right - workArea.left);
		const int rawWorkHeight = static_cast<int>(workArea.bottom - workArea.top);
		const int workWidth = rawWorkWidth > 1 ? rawWorkWidth : 1;
		const int workHeight = rawWorkHeight > 1 ? rawWorkHeight : 1;
		// 高 DPI 叠加用户倍率时，启动窗口仍需完整落在工作区内。
		SettingWindowWidth = Inkeys::UI::Setting::ResolveWindowExtent(
			Inkeys::UI::Setting::DefaultWidthDip, settingGlobalScale, workWidth);
		SettingWindowHeight = Inkeys::UI::Setting::ResolveWindowExtent(
			Inkeys::UI::Setting::DefaultHeightDip, settingGlobalScale, workHeight);
		SettingWindowX = workArea.left + max(0,
			(workWidth - SettingWindowWidth) / 2);
		SettingWindowY = workArea.top + max(0,
			(workHeight - SettingWindowHeight) / 2);
	}
}

[[nodiscard]] bool AddSettingFontResource(UINT resourceId, float size,
	ImFontConfig* config, ImFont** result = nullptr)
{
	HMODULE module = GetModuleHandleW(nullptr);
	HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), L"TTF");
	if (!resource) return false;
	HGLOBAL loaded = LoadResource(module, resource);
	void* data = loaded ? LockResource(loaded) : nullptr;
	const DWORD dataSize = SizeofResource(module, resource);
	if (!data || !dataSize) return false;
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
		data, dataSize, size, config);
	if (result) *result = font;
	return font != nullptr;
}

[[nodiscard]] bool RebuildSettingFonts()
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	ImFontMain = nullptr;
	ImFontStrong = nullptr;
	ImGui::GetStyle().FontScaleDpi = settingGlobalScale;

	static constexpr ImWchar excludedGlyphs[] = { 0xe81e, 0xe81e, 0 };
	const bool traditionalChinese = I18n::isIdentifying(L"zh-TW");
	auto addTextFace = [&](UINT primaryResource, UINT fallbackResource,
		ImFont** result)
		{
			ImFontConfig textConfig;
			textConfig.OversampleH = 1;
			textConfig.OversampleV = 1;
			textConfig.RasterizerDensity = settingGlobalScale;
			textConfig.FontDataOwnedByAtlas = false;
			textConfig.GlyphExcludeRanges = excludedGlyphs;
			if (!AddSettingFontResource(primaryResource, 30.0F,
				&textConfig, result))
				return false;

			if (traditionalChinese)
			{
				// 繁体字库后合并简体字形，保持完整的中文回退。
				textConfig.MergeMode = true;
				if (!AddSettingFontResource(fallbackResource, 30.0F,
					&textConfig))
					return false;
			}

			ImFontConfig iconConfig;
			iconConfig.OversampleH = 1;
			iconConfig.OversampleV = 1;
			iconConfig.RasterizerDensity = settingGlobalScale;
			iconConfig.FontDataOwnedByAtlas = false;
			iconConfig.GlyphOffset.y = 10.0F;
			iconConfig.PixelSnapH = true;
			iconConfig.MergeMode = true;
			if (!AddSettingFontResource(257U, 36.0F, &iconConfig))
				return false;
			iconConfig.GlyphOffset.y = 4.0F;
			return AddSettingFontResource(262U, 32.0F, &iconConfig);
		};

	const UINT regularResource = traditionalChinese ? 258U : 198U;
	const UINT strongResource = traditionalChinese ? 298U : 297U;
	if (!addTextFace(regularResource, 198U, &ImFontMain)
		|| !addTextFace(strongResource, 297U, &ImFontStrong))
		return false;

	io.FontDefault = ImFontMain;
	// Strong 层级绑定真实内嵌粗体，不通过描边或重复绘制伪造字重。
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Caption,
		ImFontMain, 12.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body,
		ImFontMain, 14.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_BodyStrong,
		ImFontStrong, 14.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Subtitle,
		ImFontStrong, 20.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Title,
		ImFontStrong, 28.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_TitleLarge,
		ImFontStrong, 40.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Display,
		ImFontStrong, 68.0F);
	// Initialize/隐藏态重建都必须先完成 atlas，Show 不承担字体工作。
	return io.Fonts->Build();
}

SettingSessionCoroutine RunSettingSession()
{
	// 本作用域内所有潜在阻塞业务统一投递给单一 FIFO worker。
	#define WriteSetting QueueWriteSetting
	#define PptComWriteSetting QueuePptComWriteSetting
	#define ShellExecuteW QueueShellExecuteCompat
	#define SetStartupState QueueSetStartup
	#define RestartProgram QueueRestart
	#define CloseProgram QueueClose
	auto GetUpdateChannel = []()
		{
			shared_lock<shared_mutex> lock(setlistUpdateMutex);
			return setlist.UpdateChannel;
		};
	auto SetUpdateChannel = [](const string& channel)
		{
			unique_lock<shared_mutex> lock(setlistUpdateMutex);
			setlist.UpdateChannel = channel;
		};
	auto GetUpdateArchitecture = []()
		{
			shared_lock<shared_mutex> lock(setlistUpdateMutex);
			return setlist.updateArchitecture;
		};
	auto SetUpdateArchitecture = [](const string& architecture)
		{
			unique_lock<shared_mutex> lock(setlistUpdateMutex);
			setlist.updateArchitecture = architecture;
		};
	auto GetEnableAutoUpdate = []()
		{
			shared_lock<shared_mutex> lock(setlistUpdateMutex);
			return setlist.enableAutoUpdate;
		};
	auto SetEnableAutoUpdate = [](bool enable)
		{
			unique_lock<shared_mutex> lock(setlistUpdateMutex);
			setlist.enableAutoUpdate = enable;
		};
	{
		if (!AcquireDeviceLease(settingFrameContext.epoch))
		{
			if (IDTLogger) IDTLogger->error("[Setting] 获取共享 D3D11 设置资源失败");
			CleanupDeviceD3D();
			settingFrameResult = FrameResult::Retry;
			co_return;
		}
		settingSessionEpoch = settingFrameContext.epoch.generation;

			// 初始化
			{
				// 图像加载
				{
					Inkeys::Graphics::DibSurface SettingSign;

					if (I18n::isIdentifying(L"zh-CN")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home1_zh-CN", 700 * settingGlobalScale, 215 * settingGlobalScale);
					else if (I18n::isIdentifying(L"zh-TW")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home1_zh-TW", 700 * settingGlobalScale, 215 * settingGlobalScale);
					else LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home1_en-US", 700 * settingGlobalScale, 215 * settingGlobalScale);
					{
						int width = settingSign[1].width = SettingSign.width();
						int height = settingSign[1].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}

					if (I18n::isIdentifying(L"zh-CN")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home2_zh-CN", 770 * settingGlobalScale, 390 * settingGlobalScale);
					else if (I18n::isIdentifying(L"zh-TW")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home2_zh-TW", 770 * settingGlobalScale, 390 * settingGlobalScale);
					else LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home2_en-US", 770 * settingGlobalScale, 390 * settingGlobalScale);
					{
						int width = settingSign[2].width = SettingSign.width();
						int height = settingSign[2].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}

					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag1", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[5].width = SettingSign.width();
						int height = settingSign[5].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag2", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[6].width = SettingSign.width();
						int height = settingSign[6].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag3", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[8].width = SettingSign.width();
						int height = settingSign[8].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag4", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[10].width = SettingSign.width();
						int height = settingSign[10].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[10]);
						delete[] data;

						(void)ret;
					}

					LoadSurfaceFromResource(&SettingSign, L"PNG", L"Profile_Picture", 45 * settingGlobalScale, 45 * settingGlobalScale);
					{
						int width = settingSign[3].width = SettingSign.width();
						int height = settingSign[3].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home_Feedback", 100 * settingGlobalScale, 100 * settingGlobalScale);
					{
						int width = settingSign[7].width = SettingSign.width();
						int height = settingSign[7].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}

					LoadSurfaceFromResource(&SettingSign, L"PNG", L"SettingSponsor", 650 * settingGlobalScale, 460 * settingGlobalScale);
					{
						int width = settingSign[9].width = SettingSign.width();
						int height = settingSign[9].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

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

						(void)ret;
					}
				}
			}
			(void)LoadSettingWindowIconTexture(setting_window);
			constexpr size_t requiredTextureIndexes[] = { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10 };
			if (ranges::any_of(requiredTextureIndexes,
				[](size_t index) { return TextureSettingSign[index] == nullptr; }))
			{
				if (IDTLogger) IDTLogger->error("[Setting] 静态图片 resident 预热失败");
				CleanupSettingTextures();
				CleanupSettingTextureCache();
				CleanupDeviceD3D();
				settingFrameResult = FrameResult::Retry;
				co_return;
			}
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.IniFilename = nullptr;

		ImGui::StyleColorsLight();

		const bool win32BackendInitialized = ImGui_ImplWin32_Init(setting_window);
		const bool dx11BackendInitialized = win32BackendInitialized
			&& ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
		if (!dx11BackendInitialized || !RebuildSettingFonts()
			|| !ImGui_ImplDX11_CreateDeviceObjects())
		{
			if (IDTLogger) IDTLogger->error("[Setting] 内嵌字体图集初始化失败");
			if (dx11BackendInitialized) ImGui_ImplDX11_Shutdown();
			if (win32BackendInitialized) ImGui_ImplWin32_Shutdown();
			CleanupSettingTextures();
			CleanupSettingTextureCache();
			io.Fonts->Clear();
			ImFluent::ResetContext();
			ImGui::DestroyContext();
			CleanupDeviceD3D();
			settingFrameResult = FrameResult::Retry;
			co_return;
		}

		ApplySystemTheme(setting_window);
		settingConsumedThemeSerial = settingThemeSerial.load(memory_order_acquire);

		int QuestNumbers = 0;
		int QueryWaitingTime = 5;

		// 设置参数

		struct
		{
			bool Enable = Inkeys::config.Config.AutoClean;
		}ConfigurationSetting;

		bool EnableFixWithChangeArchitecture = true;
		bool EnableAutoUpdate = false;

		int SelectLanguage = setlist.selectLanguage;
		bool StartUp = setlist.startUp;
		bool CreateLnk = setlist.shortcutAssistant.createLnk;
		bool CorrectLnk = setlist.shortcutAssistant.correctLnk;

		int SetSkinMode = setlist.SetSkinMode;
		float SettingGlobalScale = settingUserScale;
		float BarZoom = static_cast<float>(Inkeys::config.UI.Bar.Zoom.load());
		bool BarZoomSavePending = false;

		int TopSleepTime = setlist.topSleepTime;
		bool RightClickClose = setlist.RightClickClose;
		bool BrushRecover = setlist.BrushRecover;
		bool RubberRecover = setlist.RubberRecover;
		struct
		{
			bool MoveRecover = setlist.regularSetting.moveRecover;
			bool ClickRecover = setlist.regularSetting.clickRecover;
			bool AvoidFullScreen = setlist.regularSetting.avoidFullScreen;
			int TeachingSafetyMode = setlist.regularSetting.teachingSafetyMode;
		}RegularSetting;

		struct
		{
			bool Enable = setlist.saveSetting.enable;
			int SaveDays = setlist.saveSetting.saveDays;
		}SaveSetting;

		int PaintDevice = setlist.paintDevice;
		bool LiftStraighten = setlist.liftStraighten, WaitStraighten = setlist.waitStraighten;
		bool PointAdsorption = setlist.pointAdsorption;
		bool SmoothWriting = setlist.smoothWriting;
		int EraserMode = setlist.eraserSetting.eraserMode;
		bool HideTouchPointer = setlist.hideTouchPointer;

		int PreparationQuantity = setlist.performanceSetting.preparationQuantity;
		bool SuperDraw = setlist.performanceSetting.superDraw;

		struct
		{
			bool MemoryWidth = setlist.presetSetting.memoryWidth;
			bool MemoryColor = setlist.presetSetting.memoryColor;

			bool AutoDefaultWidth = setlist.presetSetting.autoDefaultWidth;
			float DefaultBrush1Width = setlist.presetSetting.defaultBrush1Width;
			float DefaultHighlighter1Width = setlist.presetSetting.defaultHighlighter1Width;
		}PresetSetting;

		struct
		{
			struct
			{
				bool Enable = setlist.plugInSetting.superTop.enable;
				bool Indicator = setlist.plugInSetting.superTop.indicator;
			}SuperTop;
		}PlugInSetting;

		// 插件参数

		float PptUiWidgetScale = 1.0f, PptUiWidgetScaleRecord = 1.0f;
		float BottomSideBothWidgetScale = pptComSetlist.bottomSideBothWidgetScale, BottomSideBothWidgetScaleRecord = pptComSetlist.bottomSideBothWidgetScale;
		float MiddleSideBothWidgetScale = pptComSetlist.middleSideBothWidgetScale, MiddleSideBothWidgetScaleRecord = pptComSetlist.middleSideBothWidgetScale;
		float BottomSideMiddleWidgetScale = pptComSetlist.bottomSideMiddleWidgetScale, BottomSideMiddleWidgetScaleRecord = pptComSetlist.bottomSideMiddleWidgetScale;
		bool BottomSideBothWidgetScaleUnifie = true;
		bool MiddleSideBothWidgetScaleUnifie = true;
		bool BottomSideMiddleWidgetScaleUnifie = false;

		bool PptComFixedHandWriting = pptComSetlist.fixedHandWriting;
		bool PptComShowLoadingScreen = pptComSetlist.showLoadingScreen;
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

		//bool AutoKillWpsProcess = pptComSetlist.autoKillWpsProcess;

		struct
		{
			bool Enable = ddbInteractionSetList.enable;
			bool RunAsAdmin = ddbInteractionSetList.runAsAdmin;

			struct
			{
				bool SeewoWhiteboard3Floating = ddbInteractionSetList.intercept.SeewoWhiteboard3Floating;
				bool SeewoWhiteboard5Floating = ddbInteractionSetList.intercept.SeewoWhiteboard5Floating;
				bool SeewoWhiteboard5CFloating = ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating;
				bool SeewoPincoSideBarFloating = ddbInteractionSetList.intercept.SeewoPincoSideBarFloating;
				bool SeewoPincoDrawingFloating = ddbInteractionSetList.intercept.SeewoPincoDrawingFloating;
				bool SeewoPPTFloating = ddbInteractionSetList.intercept.SeewoPPTFloating;
				bool SeewoIwbAssistantFloating = ddbInteractionSetList.intercept.SeewoIwbAssistantFloating;
				bool YiouBoardFloating = ddbInteractionSetList.intercept.YiouBoardFloating;
				bool AiClassFloating = ddbInteractionSetList.intercept.AiClassFloating;
				bool ClassInXFloating = ddbInteractionSetList.intercept.ClassInXFloating;
				bool IntelligentClassFloating = ddbInteractionSetList.intercept.IntelligentClassFloating;
				bool ChangYanFloating = ddbInteractionSetList.intercept.ChangYanFloating;
				bool ChangYan5Floating = ddbInteractionSetList.intercept.ChangYan5Floating;
				bool Iclass30SidebarFloating = ddbInteractionSetList.intercept.Iclass30SidebarFloating;
				bool Iclass30Floating = ddbInteractionSetList.intercept.Iclass30Floating;
				bool SeewoDesktopSideBarFloating = ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating;
				bool SeewoDesktopDrawingFloating = ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating;
			}intercept;
		} Ddb;

		// 组件参数
		bool ComponentShortcutButtonApplianceExplorer = setlist.component.shortcutButton.appliance.explorer;
		bool ComponentShortcutButtonApplianceTaskmgr = setlist.component.shortcutButton.appliance.taskmgr;
		bool ComponentShortcutButtonApplianceControl = setlist.component.shortcutButton.appliance.control;
		bool ComponentShortcutButtonSystemDesktop = setlist.component.shortcutButton.system.desktop;
		bool ComponentShortcutButtonSystemLockWorkStation = setlist.component.shortcutButton.system.lockWorkStation;
		bool ComponentShortcutButtonKeyboardKeyboardesc = setlist.component.shortcutButton.keyboard.keyboardesc;
		bool ComponentShortcutButtonKeyboardKeyboardAltF4 = setlist.component.shortcutButton.keyboard.keyboardAltF4;
		bool ComponentShortcutButtonRollCallIslandCaller1 = setlist.component.shortcutButton.rollCall.IslandCaller1;
		bool ComponentShortcutButtonRollCallIslandCaller2 = setlist.component.shortcutButton.rollCall.IslandCaller2;
		bool ComponentShortcutButtonRollCallSecRandom1 = setlist.component.shortcutButton.rollCall.SecRandom1;
		bool ComponentShortcutButtonRollCallSecRandom2 = setlist.component.shortcutButton.rollCall.SecRandom2;
		bool ComponentShortcutButtonRollCallSecRandom2Compat = setlist.component.shortcutButton.rollCall.SecRandom2Compat;
		bool ComponentShortcutButtonRollCallNamePicker = setlist.component.shortcutButton.rollCall.NamePicker;
		bool ComponentShortcutButtonLinkageClassislandSettings = setlist.component.shortcutButton.linkage.classislandSettings;
		bool ComponentShortcutButtonLinkageClassislandProfile = setlist.component.shortcutButton.linkage.classislandProfile;
		bool ComponentShortcutButtonLinkageClassislandClassswap = setlist.component.shortcutButton.linkage.classislandClassswap;

		// 实验选项
		struct
		{
			struct
			{
				bool AnimationEnable = Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable;
				float AnimationSpeedRate = static_cast<float>(clamp(
					static_cast<double>(Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate), 0.1, 5.0));
				bool AnimationSpeedSavePending = false;
				bool EdgeLightingEnable = Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable;
				bool DynamicEdgeLighting = Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic;
				bool DebugMode = Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable;
				bool ShowFrameRate = Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate;
			}Inkeys3;
		}Experimental;

		// ==========

		wstring receivedData;

		int settingTab = 0;
		int settingPlugInTab = 0;
		int transitionTab = settingTab;
		auto transitionStarted = chrono::steady_clock::now();

		// 首次恢复只完成常驻资源预热；Show 后才创建交换链并进入绘制循环。
		settingFrameResult = FrameResult::Idle;
		co_await suspend_always{};

		while (!settingSessionShouldStop.load(memory_order_acquire))
		{
			// Start the Dear ImGui frame
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			// 所有页面默认使用 ImFluent Body 字体，局部字号只通过 scoped PushFont 切换。
			ImFluent::PushFluentStyle();
			EnableAutoUpdate = GetEnableAutoUpdate();

			{
				//定义栏操作
				enum settingTabEnum
				{
					tab1,
					Language,
					tabCICD,
					tabConfiguration,
					tab2,
					tab3,
					tabPerformance,
					tabPreset,
					tab4,
					tabComponent,
					tab5,
					tab6,
					tab8,
					tab9,
					tabExperimental,
				};
				enum settingPlugInTabEnum
				{
					tabPlug1,
					tabPlug2,
					tabPlug3,
					tabSuperTop,
					tabPlugDDB
				};

				ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
				ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

				ImGui::PushStyleColor(ImGuiCol_Border,
					Widgets::Color(ImFluentCol_SurfaceStrokeDefault));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
				ImGui::Begin("主窗口", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);//开始绘制窗口
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();

				const auto navigationLayout = Inkeys::UI::Setting::ResolveNavigationLayout(
					static_cast<float>(SettingWindowWidth), settingGlobalScale);
				static bool narrowPaneOpen = false;
				static auto previousNavigationLayout = Inkeys::UI::Setting::NavigationLayout::Open;
				static ImFluentNavViewMode desktopNavigationMode = ImFluentNavViewMode_LeftOpen;
				if (navigationLayout != Inkeys::UI::Setting::NavigationLayout::Overlay
					&& previousNavigationLayout != navigationLayout)
				{
					desktopNavigationMode = navigationLayout
						== Inkeys::UI::Setting::NavigationLayout::Open
						? ImFluentNavViewMode_LeftOpen : ImFluentNavViewMode_LeftCompact;
				}
				previousNavigationLayout = navigationLayout;
				const bool overlayNavigation = navigationLayout
					== Inkeys::UI::Setting::NavigationLayout::Overlay;

				// TitleBar 与三个 caption buttons 始终由 Inkeys 自绘；DWM 只做
				// 可选的外框/阴影/圆角增强，因此 Win7 关闭 Aero 时也不会退回
				// 系统标题栏。
				constexpr bool customTitleBar = true;
				if (customTitleBar)
				{
					const string titleText = IA(I18nKey.SettingsUI.N);
					const string versionLabel = utf16ToUtf8(editionVersion);
					ImFluent::PushFont(ImFluentTextStyle_Caption);
					const float titleTextWidth = ImGui::CalcTextSize(titleText.c_str()).x;
					ImFluent::PopFont();
					ImFluent::PushFont(ImFluentTextStyle_Caption);
					const float versionTextWidth = ImGui::CalcTextSize(versionLabel.c_str()).x;
					ImFluent::PopFont();
					const auto titleBarGeometry = ResolveSettingTitleBarGeometry(
						setting_window, titleTextWidth, versionTextWidth);
					if (ImFluent::BeginTitleBar(nullptr, titleBarGeometry.height))
					{
						const ImVec2 origin = ImGui::GetWindowPos();
						ImDrawList* drawList = ImGui::GetWindowDrawList();
						const bool active = settingWindowActive.load(memory_order_acquire);
						const ImRect iconBounds = ToImRect(titleBarGeometry.icon, origin);
						if (TextureSettingSign[0])
						{
							drawList->AddImage((ImTextureID)(intptr_t)TextureSettingSign[0],
								iconBounds.Min, iconBounds.Max, { 0.0F, 0.0F }, { 1.0F, 1.0F },
								IM_COL32(255, 255, 255, active ? 255 : 140));
						}

						ImFluent::PushFont(ImFluentTextStyle_Caption);
						const float titleLeft = titleBarGeometry.icon.right
							+ Inkeys::UI::Setting::TitleBarContentSpacingDip * settingGlobalScale;
						const ImVec4 titleClip{ origin.x + titleLeft, origin.y,
							origin.x + titleBarGeometry.identity.right,
							origin.y + titleBarGeometry.height };
						const ImU32 titleColor = SettingChromeColor(
							ImFluentCol_TextPrimary, active ? 1.0F : 0.55F);
						drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
							{ origin.x + titleLeft,
								origin.y + (titleBarGeometry.height - ImGui::GetFontSize()) * 0.5F },
							titleColor, titleText.c_str(), nullptr, 0.0F, &titleClip);
						ImFluent::PopFont();

						if (titleBarGeometry.versionVisible)
						{
							const ImRect versionBounds = ToImRect(titleBarGeometry.version, origin);
							ImGui::SetCursorScreenPos(versionBounds.Min);
							if (ImGui::InvisibleButton("##setting-titlebar-version",
								versionBounds.GetSize()))
							{
								// RightHeader 版本入口与导航中的“软件版本”复用同一路由。
								settingTab = settingTabEnum::tab6;
							}
							const bool versionHovered = ImGui::IsItemHovered();
							const bool versionHeld = ImGui::IsItemActive();
							if (versionHovered)
							{
								const float insetY = 4.0F * settingGlobalScale;
								drawList->AddRectFilled(
									{ versionBounds.Min.x, versionBounds.Min.y + insetY },
									{ versionBounds.Max.x, versionBounds.Max.y - insetY },
									SettingChromeColor(versionHeld
										? ImFluentCol_SubtleFillTertiary
										: ImFluentCol_SubtleFillSecondary),
									4.0F * settingGlobalScale);
							}
							ImFluent::PushFont(ImFluentTextStyle_Caption);
							const ImVec2 versionSize = ImGui::CalcTextSize(versionLabel.c_str());
							drawList->AddText({
								versionBounds.Min.x + (versionBounds.GetWidth() - versionSize.x) * 0.5F,
								versionBounds.Min.y + (versionBounds.GetHeight() - versionSize.y) * 0.5F },
								SettingChromeColor(ImFluentCol_TextSecondary,
									active ? 1.0F : 0.55F), versionLabel.c_str());
							ImFluent::PopFont();
						}

						ImFluent::PushFont(ImFluentTextStyle_Body);
						RenderSettingCaptionButton(drawList, setting_window, origin,
							titleBarGeometry.minimize, "\xee\xa4\xa1", false, active);
						RenderSettingCaptionButton(drawList, setting_window, origin,
							titleBarGeometry.maximize,
							IsZoomed(setting_window) ? "\xee\xa4\xa3" : "\xee\xa4\xa2",
							false, active);
						RenderSettingCaptionButton(drawList, setting_window, origin,
							titleBarGeometry.close, "\xee\xa2\xbb", true, active);
						ImFluent::PopFont();
						ImFluent::EndTitleBar();
					}
				}

				// 三档导航复用同一条目源，并由 ImFluent 统一承载 pane 与 content。
				bool navigationActivated = false;
				auto renderNavigationItems = [&]()
					{
						auto page = [&](int target, const char* label, const char* glyph)
					{
						if (ImFluent::NavItem(label, settingTab == target, glyph))
						{
							settingTab = target;
							navigationActivated = true;
							if (target == settingTabEnum::tab4)
								settingPlugInTab = settingPlugInTabEnum::tabPlug1;
						}
					};
					auto action = [&](const char* label, const char* glyph, auto&& callback)
					{
						if (!ImFluent::NavItem(label, false, glyph)) return;
						navigationActivated = true;
						callback();
					};

						// 第三个间距覆盖子区收尾和分隔线占位，避免底部操作被裁切。
						const float footerHeight = Widgets::Dip(
							ImFluent::GetStyle().NavItemHeight * 3.0F
							+ ImFluent::GetStyle().SpacingMedium * 3.0F);
						const float pageListHeight = max(0.0F,
							ImGui::GetContentRegionAvail().y - footerHeight);
						// 子滚动区沿用导航 pane 的内边距，保证主条目与底部操作对齐。
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
						ImGui::BeginChild("##setting-navigation-pages", { 0.0F, pageListHeight });
						ImGui::PopStyleVar();
						page(settingTabEnum::tab1, IA(I18nKey.SettingsUI.Home.N).c_str(), "\ue80f");
						ImFluent::NavSubHeader(IA(I18nKey.SettingsUI.N).c_str());
						page(settingTabEnum::Language, IA(I18nKey.SettingsUI.Language.N).c_str(), "\ue774");
						page(settingTabEnum::tabConfiguration, IA(I18nKey.SettingsUI.Configuration.N).c_str(), "\ue81e");
						page(settingTabEnum::tab6, IA(I18nKey.SettingsUI.Version.N).c_str(), "\ue946");
						page(settingTabEnum::tab2, IA(I18nKey.SettingsUI.Regular.N).c_str(), "\ue7b8");
						page(settingTabEnum::tab3, IA(I18nKey.SettingsUI.Draw.N).c_str(), "\uee56");
						page(settingTabEnum::tabPreset, IA(I18nKey.SettingsUI.Preset.N).c_str(), "\uf259");
						page(settingTabEnum::tab4, IA(I18nKey.SettingsUI.PlugIn.N).c_str(), "\ue74c");
						page(settingTabEnum::tabComponent, IA(I18nKey.SettingsUI.Component.N).c_str(), "\ue70b");
						page(settingTabEnum::tab5, IA(I18nKey.SettingsUI.HotKey.N).c_str(), "\ue765");
						page(settingTabEnum::tabExperimental, "实验选项", "\uec4a");
						page(settingTabEnum::tab8, IA(I18nKey.SettingsUI.Sponsor.N).c_str(), "\ue789");
						page(settingTabEnum::tab9, IA(I18nKey.SettingsUI.DebugSoftware.N).c_str(), "\ue90f");
						ImGui::EndChild();
						ImFluent::Separator();
						action(IA(I18nKey.SettingsUI.Community.N).c_str(), "\ue716", [&]
						{
							if (I18n::isIdentifying(L"zh-CN"))
								ShellExecuteW(0, 0, L"https://www.inkeys.top/community.html", 0, 0, SW_SHOW);
							else
								ShellExecuteW(0, 0, L"https://en.inkeys.top/community.html", 0, 0, SW_SHOW);
						});
					action(IA(I18nKey.SettingsUI.RestartSoftware.N).c_str(), "\ue72c", [&]
						{
							Inkeys::UI::Setting::Hide();
							RestartProgram();
						});
					action(IA(I18nKey.SettingsUI.ExitSoftware.N).c_str(), "\ue711", [&]
						{
							Inkeys::UI::Setting::Hide();
							CloseProgram();
						});
					};

				ImGui::SetCursorPos({ 0.0F, customTitleBar
					? Inkeys::UI::Setting::TitleBarHeightDip * settingGlobalScale : 0.0F });
				if (navigationLayout != Inkeys::UI::Setting::NavigationLayout::Overlay)
				{
					ImFluent::BeginNavigationView("##setting-navigation", &desktopNavigationMode);
					renderNavigationItems();
					ImFluent::EndNavigationView();
				}
				if (overlayNavigation)
				{
					// 窄屏内容与导航共享 CompactOverlay，pane 在内容之后绘制以保持覆盖层级。
					ImFluent::BeginSplitView("##setting-responsive-nav", &narrowPaneOpen,
						ImFluentSplitViewDisplayMode_CompactOverlay,
						ImFluentSplitViewPanePlacement_Left);
					ImFluent::BeginSplitViewContent();
				}
				ImFluent::NavigationViewBeginContent();

				// 页面切换只推进两个标量，避免动画期间加载或分配图形资源。
				if (transitionTab != settingTab)
				{
					transitionTab = settingTab;
					transitionStarted = chrono::steady_clock::now();
				}
				const float transitionProgress =
					Inkeys::UI::Setting::ResolvePageTransitionProgress(chrono::duration<float>(
						chrono::steady_clock::now() - transitionStarted).count());
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
					ImGui::GetStyle().Alpha * (0.55F + transitionProgress * 0.45F));
				const float availablePageWidth = ImGui::GetContentRegionAvail().x;
				const float pageWidth = Inkeys::UI::Setting::ResolvePageWidth(
					availablePageWidth, settingGlobalScale);
				const float pageOffset = max(0.0F, (availablePageWidth - pageWidth) * 0.5F)
					+ Widgets::Dip((1.0F - transitionProgress) * 8.0F);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pageOffset);
				(void)ImGui::BeginChild("##setting-page-content", { pageWidth, 0.0F },
					ImGuiChildFlags_AutoResizeY);

				// 主版块 765<-750(770 主页) * 608
				switch (settingTab)
				{
					// 主页
				case settingTabEnum::tab1:
				{
					Widgets::PageHeader("Inkeys",
						IA(I18nKey.SettingsUI.Home.Prompt).c_str(), true);
					ImFluent::Separator();
					Widgets::SectionHeader("快速访问");
					const float linkButtonWidth = Widgets::Dip(148.0F);
					const ImVec2 linkButtonSize(linkButtonWidth, Widgets::Dip(40.0F));
					ImFluent::BeginWrapPanel(Widgets::Dip(8.0F), Widgets::Dip(8.0F));
					ImFluent::WrapPanelNextItem(linkButtonWidth);
					if (ImFluent::Button("\uf900  Website", linkButtonSize))
					{
						if (I18n::isIdentifying(L"zh-CN")) ShellExecuteW(0, 0, L"https://www.inkeys.top", 0, 0, SW_SHOW);
						else ShellExecuteW(0, 0, L"https://en.inkeys.top", 0, 0, SW_SHOW);
					}
					ImFluent::WrapPanelNextItem(linkButtonWidth);
					if (ImFluent::Button("\uf901  GitHub", linkButtonSize))
						ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/Inkeys", 0, 0, SW_SHOW);
					ImFluent::WrapPanelNextItem(linkButtonWidth);
					if (ImFluent::Button("\uf904  Community", linkButtonSize))
					{
						if (I18n::isIdentifying(L"zh-CN")) ShellExecuteW(0, 0, L"https://www.inkeys.top/community.html", 0, 0, SW_SHOW);
						else ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/Inkeys/discussions", 0, 0, SW_SHOW);
					}
					ImFluent::WrapPanelNextItem(linkButtonWidth);
					if (ImFluent::Button("\uf905  Bilibili", linkButtonSize))
						ShellExecuteW(0, 0, L"https://space.bilibili.com/1330313497", 0, 0, SW_SHOW);
					ImFluent::WrapPanelNextItem(linkButtonWidth);
					if (ImFluent::Button("\uf906  Feedback", linkButtonSize))
						ShellExecuteW(0, 0, L"https://www.wjx.cn/vm/mqNTTRL.aspx#", 0, 0, SW_SHOW);
					ImFluent::EndWrapPanel();

					Widgets::SectionHeader("关于作者");
					if (Widgets::BeginSettingsCard("##home-author", "AlanCRL",
						IA(I18nKey.SettingsUI.Home.Developer).c_str(), "\uf902"))
					{
						if (ImFluent::HyperlinkButton("联系作者"))
							ShellExecuteW(0, 0, L"mailto:alan-crl@foxmail.com", 0, 0, SW_SHOW);
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader("使用教程");
					const float tutorialWidth = min(ImGui::GetContentRegionAvail().x,
						Widgets::Dip(760.0F));
					const bool tutorialCardVisible = ImFluent::BeginCard("##home-tutorial",
						{ tutorialWidth, 0.0F }, ImFluentCardStyle_Filled);
					if (tutorialCardVisible)
					{
						const float imageWidth = ImGui::GetContentRegionAvail().x;
						const float sourceWidth = static_cast<float>(max(1, settingSign[1].width));
						const float sourceHeight = static_cast<float>(max(1, settingSign[1].height));
						ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[1],
							{ imageWidth, imageWidth * sourceHeight / sourceWidth });
						ImFluent::TextBlockColored("后续教程内容将在此区域持续补充。",
							Widgets::Color(ImFluentCol_TextSecondary), ImFluentTextStyle_Caption);
					}
					ImFluent::EndCard();

					break;
				}

				// 语言
				case settingTabEnum::Language:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Language.N).c_str());
					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Language.UI.N).c_str());

					if (Widgets::BeginSettingsCard("##language-ui",
						IA(I18nKey.SettingsUI.Language.UI.Select).c_str(),
						IA(I18nKey.SettingsUI.Language.UI.SelectE).c_str(), "\ue774"))
					{
						vector<string> languages{
							IA(I18nKey.SettingsUI.Language.UI.Language.en_US),
							IA(I18nKey.SettingsUI.Language.UI.Language.zh_CN),
							IA(I18nKey.SettingsUI.Language.UI.Language.zh_TW)
						};
						const int previousLanguage = SelectLanguage;
						if (Widgets::combo.Select("##language-select", &SelectLanguage, languages)
							&& previousLanguage != SelectLanguage
							&& setlist.selectLanguage != SelectLanguage)
						{
							setlist.selectLanguage = SelectLanguage;
							WriteSetting();
							if (SelectLanguage == 1) I18n::load(1, L"JSON", L"zh-CN");
							else if (SelectLanguage == 2) I18n::load(1, L"JSON", L"zh-TW");
							else I18n::load(1, L"JSON", L"en-US");
							QueueConfirmRestart(IW(I18nKey.SettingsUI.Language.UI.Warn));
						}
						Widgets::EndSettingsCard();
					}
					break;
				}
				// 配置保存
				case settingTabEnum::tabConfiguration:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Configuration.N).c_str());
					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Configuration.Clean.N).c_str());

					if (Widgets::BeginSettingsCard("##configuration-clean",
						IA(I18nKey.SettingsUI.Configuration.Clean.Enable).c_str(),
						IA(I18nKey.SettingsUI.Configuration.Clean.EnableE).c_str(), "\ue74d"))
					{
						ImFluent::ToggleSwitch("##configuration-clean-toggle",
							&ConfigurationSetting.Enable, "", "");
						if (Inkeys::config.Config.AutoClean != ConfigurationSetting.Enable)
						{
							Inkeys::config.Config.AutoClean = ConfigurationSetting.Enable;
							QueueConfigWrite();
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(
						IA(I18nKey.SettingsUI.Configuration.CanvasSave.N).c_str());
					if (Widgets::BeginSettingsCard("##configuration-history",
						IA(I18nKey.SettingsUI.Configuration.CanvasSave.Enable).c_str(),
						IA(I18nKey.SettingsUI.Configuration.CanvasSave.EnableE).c_str(), "\ue81c"))
					{
						ImFluent::ToggleSwitch("##configuration-history-toggle",
							&SaveSetting.Enable, "", "");
						if (setlist.saveSetting.enable != SaveSetting.Enable)
						{
							setlist.saveSetting.enable = SaveSetting.Enable;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					if (Widgets::BeginSettingsCard("##configuration-retention",
						IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.N).c_str(),
						IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.E).c_str(), "\ue823"))
					{
						vector<string> retentionOptions{
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_1d),
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_3d),
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_5d),
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_10d),
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_30d),
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.Never)
						};
						const int previousSaveDays = SaveSetting.SaveDays;
						if (Widgets::combo.Select("##configuration-retention-select",
							&SaveSetting.SaveDays, retentionOptions)
							&& previousSaveDays != SaveSetting.SaveDays
							&& setlist.saveSetting.saveDays != SaveSetting.SaveDays)
						{
							setlist.saveSetting.saveDays = SaveSetting.SaveDays;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					break;
				}
				// 软件版本
				case settingTabEnum::tab6:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Version.N).c_str());

					if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNew
						&& ImFluent::InfoBar(ImFluentInfoSeverity_Warning,
							IA(I18nKey.SettingsUI.Version.ManualUpdate.N).c_str(),
							"Inkeys 已发现可用更新。",
							nullptr, nullptr, true,
							IA(I18nKey.SettingsUI.Version.ManualUpdate.ManualUpdate).c_str()))
					{
						mandatoryUpdate = true;
						AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
					}
					if (inconsistentArchitecture)
					{
						ImFluent::InfoBar(ImFluentInfoSeverity_Warning,
							IA(I18nKey.SettingsUI.Version.N).c_str(),
							IA(I18nKey.SettingsUI.Version.VersionTip).c_str());
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Version.Info.N).c_str());
					const bool versionCardVisible = ImFluent::BeginCard("##version-info",
						{ 0.0F, 0.0F }, ImFluentCardStyle_Filled);
					if (versionCardVisible)
					{
						const float logoWidth = min(96.0F * settingGlobalScale,
							ImGui::GetContentRegionAvail().x);
						if (TextureSettingSign[1])
						{
							const float sourceWidth = static_cast<float>(
								max(1, settingSign[1].width));
							const float sourceHeight = static_cast<float>(
								max(1, settingSign[1].height));
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[1],
								{ logoWidth, logoWidth * sourceHeight / sourceWidth });
						}

						wstring versionText;
						versionText += IW(I18nKey.SettingsUI.Version.Info.ReleaseVersion) + L" "
							+ editionVersion + L"(" + editionDate + L")\n";
						versionText += IW(I18nKey.SettingsUI.Version.Info.ReleaseDate) + L" "
							+ buildTime + L"\n";
						versionText += IW(I18nKey.SettingsUI.Version.Info.ReleaseArch) + L" "
							+ programArchitecture + L" | " + targetArchitecture + L"\n";
					#ifdef IDT_RELEASE
						versionText += IW(I18nKey.SettingsUI.Version.Info.ReleaseTag);
					#else
						versionText += IW(I18nKey.SettingsUI.Version.Info.DebugTag);
					#endif
						const string versionTextUtf8 = utf16ToUtf8(versionText);
						ImGui::TextWrapped("%s", versionTextUtf8.c_str());
						if (settingCICD.url.empty())
						{
							ImFluent::TextBlockColored(
								IA(I18nKey.SettingsUI.Version.Info.ManualBuild).c_str(),
								Widgets::Color(ImFluentCol_TextSecondary),
								ImFluentTextStyle_Caption);
						}
						else
						{
							ImFluent::TextBlockColored(
								IA(I18nKey.SettingsUI.Version.Info.AutoBuild).c_str(),
								Widgets::Color(ImFluentCol_TextSecondary),
								ImFluentTextStyle_Caption);
							if (ImFluent::HyperlinkButton(
								IA(I18nKey.SettingsUI.Version.Info.CICDInfo).c_str()))
								settingTab = settingTabEnum::tabCICD;
						}
					}
					ImFluent::EndCard();

					Widgets::SectionHeader(
						IA(I18nKey.SettingsUI.Version.UserInfo.N).c_str());
					const string userIdDescription = IA(I18nKey.SettingsUI.Version.UserInfo.UserId)
						+ " " + utf16ToUtf8(userId);
					if (Widgets::BeginSettingsCard("##version-user-id",
						IA(I18nKey.SettingsUI.Version.UserInfo.CopyUserId).c_str(),
						userIdDescription.c_str(), "\ue8c8"))
					{
						if (ImFluent::Button(IA(I18nKey.Operate.Copy).c_str()))
						{
							OpenClipboard(nullptr);
							EmptyClipboard();
							const size_t size = (userId.length() + 1) * sizeof(wchar_t);
							HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
							if (hGlobal)
							{
								wchar_t* destination = static_cast<wchar_t*>(GlobalLock(hGlobal));
								if (destination)
								{
									wcscpy_s(destination, userId.length() + 1, userId.c_str());
									GlobalUnlock(hGlobal);
									SetClipboardData(CF_UNICODETEXT, hGlobal);
								}
								else
								{
									GlobalFree(hGlobal);
								}
							}
							CloseClipboard();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Version.Repair.N).c_str());
					if (Widgets::BeginSettingsCard("##version-repair",
						IA(I18nKey.SettingsUI.Version.Repair.RepairSoftware).c_str(),
						IA(I18nKey.SettingsUI.Version.Repair.RepairSoftwareE).c_str(), "\ue90f"))
					{
						if (ImFluent::Button(IA(I18nKey.Operate.Repair).c_str()))
						{
							if (EnableFixWithChangeArchitecture)
							{
								if (targetArchitecture == L"win64") SetUpdateArchitecture("win64");
								else if (targetArchitecture == L"arm64") SetUpdateArchitecture("arm64");
								else SetUpdateArchitecture("win32");
								WriteSetting();
							}
							mandatoryUpdate = true;
							if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNotStarted)
								QueueAutomaticUpdate();
							else
								AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##version-repair-architecture",
						IA(I18nKey.SettingsUI.Version.Repair.RepairArch).c_str(), nullptr, "\ue8a9"))
					{
						ImFluent::ToggleSwitch("##version-repair-architecture-toggle",
							&EnableFixWithChangeArchitecture, "", "");
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Version.Update.N).c_str());
					if (Widgets::BeginSettingsCard("##version-auto-update",
						IA(I18nKey.SettingsUI.Version.Update.AutoUpate).c_str(), nullptr, "\ue895"))
					{
						const bool previousAutoUpdate = EnableAutoUpdate;
						ImFluent::ToggleSwitch("##version-auto-update-toggle",
							&EnableAutoUpdate, "", "");
						if (previousAutoUpdate != EnableAutoUpdate)
						{
							if (!EnableAutoUpdate
								&& AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateRestart)
							{
								SettingBusinessCommand command;
								command.kind = SettingBusinessKind::ClearInstallerAndSetAutoUpdate;
								command.flag = EnableAutoUpdate;
								QueueBusiness(std::move(command));
								AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
							}
							else
							{
								SetEnableAutoUpdate(EnableAutoUpdate);
								WriteSetting();
								if (EnableAutoUpdate
									&& AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNew)
									AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
							}
						}
						Widgets::EndSettingsCard();
					}
					ImFluent::InfoBar(ImFluentInfoSeverity_Informational,
						IA(I18nKey.SettingsUI.Version.Update.Channel.N).c_str(),
						IA(I18nKey.SettingsUI.Version.Update.ChannelTip).c_str());
					if (Widgets::BeginSettingsCard("##version-update-channel",
						IA(I18nKey.SettingsUI.Version.Update.Channel.N).c_str(), nullptr, "\ue713"))
					{
						vector<string> channels{
							IA(I18nKey.SettingsUI.Version.Update.Channel.LTS),
							IA(I18nKey.SettingsUI.Version.Update.Channel.Insider),
							IA(I18nKey.SettingsUI.Version.Update.Channel.Canary)
						};
						const string currentChannel = GetUpdateChannel();
						int channelIndex = currentChannel == "Insider" ? 1
							: (currentChannel == "Canary" ? 2 : 0);
						const int previousChannelIndex = channelIndex;
						if (Widgets::combo.Select("##version-update-channel-select",
							&channelIndex, channels) && previousChannelIndex != channelIndex)
						{
							const string selectedChannel = channelIndex == 1 ? "Insider"
								: (channelIndex == 2 ? "Canary" : "LTS");
							if (selectedChannel != currentChannel)
							{
								if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateRestart)
								{
									SettingBusinessCommand command;
									command.kind = SettingBusinessKind::ClearInstallerAndSetChannel;
									command.value = selectedChannel;
									QueueBusiness(std::move(command));
									AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
								}
								else
								{
									SetUpdateChannel(selectedChannel);
									WriteSetting();
									if (AutomaticUpdateState != AutomaticUpdateStateEnum::UpdateNotStarted)
										AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
								}
							}
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##version-update-architecture",
						IA(I18nKey.SettingsUI.Version.Update.Arch.N).c_str(),
						IA(I18nKey.SettingsUI.Version.Update.Arch.E).c_str(), "\ue8a9"))
					{
						vector<string> architectures{
							IA(I18nKey.SettingsUI.Version.Update.Arch.K_64),
							IA(I18nKey.SettingsUI.Version.Update.Arch.K_32),
							IA(I18nKey.SettingsUI.Version.Update.Arch.Arm64)
						};
						const string currentArchitecture = GetUpdateArchitecture();
						int architectureIndex = currentArchitecture == "win64" ? 0
							: (currentArchitecture == "arm64" ? 2 : 1);
						const int previousArchitectureIndex = architectureIndex;
						if (Widgets::combo.Select("##version-update-architecture-select",
							&architectureIndex, architectures)
							&& previousArchitectureIndex != architectureIndex)
						{
							const string selectedArchitecture = architectureIndex == 0 ? "win64"
								: (architectureIndex == 2 ? "arm64" : "win32");
							if (selectedArchitecture != currentArchitecture)
							{
								if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateRestart)
								{
									SettingBusinessCommand command;
									command.kind = SettingBusinessKind::ClearInstallerAndSetArchitecture;
									command.value = selectedArchitecture;
									QueueBusiness(std::move(command));
									AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
								}
								else
								{
									SetUpdateArchitecture(selectedArchitecture);
									WriteSetting();
									if (AutomaticUpdateState != AutomaticUpdateStateEnum::UpdateNotStarted)
										AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
								}
							}
						}
						Widgets::EndSettingsCard();
					}
					break;
				}

				// CI/CD 构建详情
				case settingTabEnum::tabCICD:
				{
					if (ImFluent::Button("\ue72b##cicd-back",
						{ 36.0F * settingGlobalScale, 36.0F * settingGlobalScale }))
						settingTab = settingTabEnum::tab6;
					ImGui::SameLine(0.0F, 12.0F * settingGlobalScale);
					Widgets::PageHeader(IA(I18nKey.SettingsUI.CICD.N).c_str(), "CI/CD");
					Widgets::PageContentStart();

					const bool cicdCardVisible = ImFluent::BeginCard("##cicd-details",
						{ 0.0F, 0.0F }, ImFluentCardStyle_Outlined);
					if (cicdCardVisible)
					{
						ImFluent::TextBlock("CI/CD", ImFluentTextStyle_BodyStrong);
						if (ImFluent::HyperlinkButton(utf16ToUtf8(settingCICD.url).c_str()))
							ShellExecuteW(nullptr, nullptr, settingCICD.url.c_str(), nullptr, nullptr, SW_SHOW);
						ImFluent::TextBlock(IA(I18nKey.SettingsUI.CICD.Repository).c_str(),
							ImFluentTextStyle_BodyStrong);
						if (ImFluent::HyperlinkButton(utf16ToUtf8(settingCICD.repoUrl).c_str()))
							ShellExecuteW(nullptr, nullptr, settingCICD.repoUrl.c_str(), nullptr, nullptr, SW_SHOW);

						wstring details;
						details += IW(I18nKey.SettingsUI.CICD.Branch) + L": " + settingCICD.branch + L"\n";
						details += IW(I18nKey.SettingsUI.CICD.Submitter) + L": " + settingCICD.submitter + L"\n";
						details += IW(I18nKey.SettingsUI.CICD.BuildTime) + L": " + settingCICD.buildTime + L"\n\n";
						details += IW(I18nKey.SettingsUI.CICD.BuildSystem) + L": " + settingCICD.buildOS + L"\n";
						details += IW(I18nKey.SettingsUI.CICD.BuildSystemVersion) + L": "
							+ settingCICD.buildOSVersion + L"\n";
						details += IW(I18nKey.SettingsUI.CICD.RunnerImageSystem) + L": "
							+ settingCICD.buildRunnerImageOS + L"\n";
						details += IW(I18nKey.SettingsUI.CICD.RunnerImageVersion) + L": "
							+ settingCICD.buildRunnerImageVersion + L"\n\n";
						details += IW(I18nKey.SettingsUI.CICD.MSBuildVersion) + L"\n"
							+ settingCICD.msBuildVersion;
						const string detailsUtf8 = utf16ToUtf8(details);
						ImGui::TextWrapped("%s", detailsUtf8.c_str());
					}
					ImFluent::EndCard();
					break;
				}

				// ---------------------

				// 常规
				case settingTabEnum::tab2:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Regular.N).c_str());

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Regular.StartUp.N).c_str());
					if (Widgets::BeginSettingsCard("##regular-startup",
						IA(I18nKey.SettingsUI.Regular.StartUp.AutoStart).c_str(),
						IA(I18nKey.SettingsUI.Regular.StartUp.AutoStartE).c_str(), "\ue7e8"))
					{
						ImFluent::ToggleSwitch("##regular-startup-toggle", &StartUp, "", "");
						if (setlist.startUp != StartUp)
						{
							SetStartupState(StartUp, GetCurrentExePath(), L"$Inkeys");
							setlist.startUp = StartUp;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##regular-shortcut",
						IA(I18nKey.SettingsUI.Regular.StartUp.Link.N).c_str(),
						IA(I18nKey.SettingsUI.Regular.StartUp.Link.E).c_str(), "\ue71b"))
					{
						ImFluent::BeginStackPanelHorizontal(Widgets::Dip(8.0F));
						if (ImFluent::AccentButton(IA(I18nKey.Operate.Create).c_str()))
						{
							wchar_t desktopPath[MAX_PATH];
							if (SHGetSpecialFolderPathW(0, desktopPath, CSIDL_DESKTOP, FALSE))
							{
								SettingBusinessCommand command;
								command.kind = SettingBusinessKind::CreateShortcut;
								command.text = wstring(desktopPath) + L"\\"
									+ IW(I18nKey.Widget.LnkName) + L".lnk";
								command.directory = GetCurrentExePath();
								QueueBusiness(std::move(command));
							}
						}
						if (ImFluent::Button(IA(I18nKey.SettingsUI.Regular.StartUp.Link.More).c_str()))
						{
							settingPlugInTab = settingPlugInTabEnum::tabPlug3;
							settingTab = settingTabEnum::tab4;
						}
						ImFluent::EndStackPanel();
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Regular.Appearance.N).c_str());
					if (Widgets::BeginSettingsCard("##regular-bar-scale",
						IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.N).c_str(),
						IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.E).c_str(), "\ue9a6"))
					{
						ImFluent::Slider("##regular-bar-scale-slider", &BarZoom,
							0.50F, 2.00F, "%.2f");
						BarZoom = round(BarZoom * 100.0F) / 100.0F;
						const bool isItemActive = ImGui::IsItemActive();
						if (fabs(BarZoom - static_cast<float>(Inkeys::config.UI.Bar.Zoom.load())) > 0.0001F)
						{
							Inkeys::config.UI.Bar.Zoom = static_cast<double>(BarZoom);
							Inkeys::UI::Bar::SetConfigZoom(static_cast<double>(BarZoom));
							BarZoomSavePending = true;
						}
						if (!isItemActive && BarZoomSavePending)
						{
							QueueConfigWrite();
							BarZoomSavePending = false;
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##regular-edge-light",
						"启用边缘光影", "关闭后仅保留基础边框，停用点光与柔光效果。", "\ue706"))
					{
						ImFluent::ToggleSwitch("##regular-edge-light-toggle",
							&Experimental.Inkeys3.EdgeLightingEnable, "", "");
						if (Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable
							!= Experimental.Inkeys3.EdgeLightingEnable)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable =
								Experimental.Inkeys3.EdgeLightingEnable;
							Inkeys::UI::Bar::SetEdgeLightingOptions(
								Experimental.Inkeys3.EdgeLightingEnable,
								Experimental.Inkeys3.DynamicEdgeLighting);
							QueueConfigWrite();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##regular-setting-scale",
						IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.N).c_str(),
						IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.E).c_str(), "\ue8a9"))
					{
						ImFluent::Slider("##regular-setting-scale-slider", &SettingGlobalScale,
							1.0F, 2.0F, "%.2f");
						SettingGlobalScale = round(SettingGlobalScale * 100.0F) / 100.0F;
						if (!ImGui::IsItemActive()
							&& SettingGlobalScale != setlist.settingGlobalScale)
						{
							setlist.settingGlobalScale =
								Inkeys::UI::Setting::NormalizeUserScale(SettingGlobalScale);
							UpdateSettingScale(QuerySettingDpi(setting_window));
							{
								lock_guard stateLock(settingStateMutex);
								settingSessionState.QueueFontRebuild();
							}
							Inkeys::UI::RenderPipeline::Request(
								Inkeys::UI::RenderPipeline::Client::Settings);
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Regular.Behavior.N).c_str());
					if (Widgets::BeginSettingsCard("##regular-top-window",
						IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.N).c_str(),
						IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.E).c_str(), "\ue922"))
					{
						vector<string> intervals{
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_100ms),
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_500ms),
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_1s),
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_3s),
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_5s),
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_10s),
							IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_30s)
						};
						const int previousTopSleepTime = TopSleepTime;
						if (Widgets::combo.Select("##regular-top-window-select",
							&TopSleepTime, intervals)
							&& previousTopSleepTime != TopSleepTime
							&& setlist.topSleepTime != TopSleepTime)
						{
							setlist.topSleepTime = TopSleepTime;
							WriteSetting();
							topWindowNow = true;
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##regular-right-click",
						IA(I18nKey.SettingsUI.Regular.Behavior.RightClickClose).c_str(),
						IA(I18nKey.SettingsUI.Regular.Behavior.RightClickCloseE).c_str(), "\ue8b2"))
					{
						ImFluent::ToggleSwitch("##regular-right-click-toggle",
							&RightClickClose, "", "");
						if (setlist.RightClickClose != RightClickClose)
						{
							setlist.RightClickClose = RightClickClose;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Regular.Tentative.N).c_str());
					if (Widgets::BeginSettingsCard("##regular-avoid-fullscreen",
						IA(I18nKey.SettingsUI.Regular.Tentative.AvoidFulScreen).c_str(),
						IA(I18nKey.SettingsUI.Regular.Tentative.AvoidFulScreenE).c_str(), "\ue7f4"))
					{
						ImFluent::ToggleSwitch("##regular-avoid-fullscreen-toggle",
							&RegularSetting.AvoidFullScreen, "", "");
						if (setlist.regularSetting.avoidFullScreen != RegularSetting.AvoidFullScreen)
						{
							setlist.regularSetting.avoidFullScreen = RegularSetting.AvoidFullScreen;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##regular-safety",
						IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.N).c_str(),
						IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.E).c_str(), "\ue72e"))
					{
						vector<string> safetyModes{
							IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode1),
							IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode2),
							IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode3),
							IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode4)
						};
						const int previousSafetyMode = RegularSetting.TeachingSafetyMode;
						if (Widgets::combo.Select("##regular-safety-select",
							&RegularSetting.TeachingSafetyMode, safetyModes)
							&& previousSafetyMode != RegularSetting.TeachingSafetyMode
							&& setlist.regularSetting.teachingSafetyMode
								!= RegularSetting.TeachingSafetyMode)
						{
							setlist.regularSetting.teachingSafetyMode =
								RegularSetting.TeachingSafetyMode;
							WriteSetting();
							CrashHandler::SetFlag(setlist.regularSetting.teachingSafetyMode);
						}
						Widgets::EndSettingsCard();
					}
					break;
				}
				// 绘制
				case settingTabEnum::tab3:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Draw.N).c_str());

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Draw.Effect.N).c_str());
					if (Widgets::BeginSettingsCard("##draw-device",
						IA(I18nKey.SettingsUI.Draw.Effect.Device.N).c_str(),
						IA(I18nKey.SettingsUI.Draw.Effect.Device.E).c_str(), "\ue7f8"))
					{
						vector<string> devices{
							IA(I18nKey.SettingsUI.Draw.Effect.Device.Touch),
							IA(I18nKey.SettingsUI.Draw.Effect.Device.MousePen)
						};
						const int previousPaintDevice = PaintDevice;
						if (Widgets::combo.Select("##draw-device-select", &PaintDevice, devices)
							&& previousPaintDevice != PaintDevice
							&& setlist.paintDevice != PaintDevice)
						{
							setlist.paintDevice = PaintDevice;
							WriteSetting();
							drawingScale = GetDrawingScale();
							stopTimingError = GetStopTimingError();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Draw.AIDraw.N).c_str());
					if (Widgets::BeginSettingsCard("##draw-lift-straighten",
						IA(I18nKey.SettingsUI.Draw.AIDraw.PenUp).c_str(),
						IA(I18nKey.SettingsUI.Draw.AIDraw.PenUpE).c_str(), "\ue8d3"))
					{
						ImFluent::ToggleSwitch("##draw-lift-straighten-toggle",
							&LiftStraighten, "", "");
						if (setlist.liftStraighten != LiftStraighten)
						{
							setlist.liftStraighten = LiftStraighten;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##draw-wait-straighten",
						IA(I18nKey.SettingsUI.Draw.AIDraw.PenStay).c_str(),
						IA(I18nKey.SettingsUI.Draw.AIDraw.PenStayE).c_str(), "\ue8d3"))
					{
						ImFluent::ToggleSwitch("##draw-wait-straighten-toggle",
							&WaitStraighten, "", "");
						if (setlist.waitStraighten != WaitStraighten)
						{
							setlist.waitStraighten = WaitStraighten;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##draw-endpoint",
						IA(I18nKey.SettingsUI.Draw.AIDraw.EndpointAdsorption).c_str(),
						IA(I18nKey.SettingsUI.Draw.AIDraw.EndpointAdsorptionE).c_str(), "\ue809"))
					{
						ImFluent::ToggleSwitch("##draw-endpoint-toggle",
							&PointAdsorption, "", "");
						if (setlist.pointAdsorption != PointAdsorption)
						{
							setlist.pointAdsorption = PointAdsorption;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Draw.DrawBehavior.N).c_str());
					if (Widgets::BeginSettingsCard("##draw-smooth",
						IA(I18nKey.SettingsUI.Draw.DrawBehavior.SoomthWriting).c_str(),
						nullptr, "\ue790"))
					{
						ImFluent::ToggleSwitch("##draw-smooth-toggle", &SmoothWriting, "", "");
						if (setlist.smoothWriting != SmoothWriting)
						{
							setlist.smoothWriting = SmoothWriting;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(
						IA(I18nKey.SettingsUI.Draw.RubberThickness.N).c_str());
					if (Widgets::BeginSettingsCard("##draw-eraser-mode",
						IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.N).c_str(),
						IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.E).c_str(), "\ued62"))
					{
						vector<string> eraserModes{
							IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.Mode3),
							IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.Mode2),
							IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.Mode1)
						};
						const int previousEraserMode = EraserMode;
						if (Widgets::combo.Select("##draw-eraser-mode-select",
							&EraserMode, eraserModes)
							&& previousEraserMode != EraserMode
							&& setlist.eraserSetting.eraserMode != EraserMode)
						{
							setlist.eraserSetting.eraserMode = EraserMode;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(
						IA(I18nKey.SettingsUI.Performance.DrawMode.N).c_str());
					if (Widgets::BeginSettingsCard("##draw-prepare",
						IA(I18nKey.SettingsUI.Performance.DrawMode.Prepare.N).c_str(),
						IA(I18nKey.SettingsUI.Performance.DrawMode.Prepare.E).c_str(), "\ue8fd"))
					{
						ImFluent::SliderInt("##draw-prepare-slider", &PreparationQuantity,
							0, 20, "%d");
						if (!ImGui::IsItemActive()
							&& PreparationQuantity != setlist.performanceSetting.preparationQuantity)
						{
							setlist.performanceSetting.preparationQuantity = PreparationQuantity;
							WriteSetting();
							ResetPrepareCanvas();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##draw-super",
						IA(I18nKey.SettingsUI.Performance.DrawMode.SuperDraw).c_str(),
						IA(I18nKey.SettingsUI.Performance.DrawMode.SuperDrawE).c_str(), "\ue945"))
					{
						ImFluent::ToggleSwitch("##draw-super-toggle", &SuperDraw, "", "");
						if (setlist.performanceSetting.superDraw != SuperDraw)
						{
							setlist.performanceSetting.superDraw = SuperDraw;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Draw.Tentative.N).c_str());
					if (Widgets::BeginSettingsCard("##draw-hide-pointer",
						IA(I18nKey.SettingsUI.Draw.Tentative.HideCursor).c_str(),
						IA(I18nKey.SettingsUI.Draw.Tentative.HideCursorE).c_str(), "\ue7c9"))
					{
						ImFluent::ToggleSwitch("##draw-hide-pointer-toggle",
							&HideTouchPointer, "", "");
						if (setlist.hideTouchPointer != HideTouchPointer)
						{
							setlist.hideTouchPointer = HideTouchPointer;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					break;
				}
				// 预设
				case settingTabEnum::tabPreset:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Preset.N).c_str());
					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Preset.Memory.N).c_str());
					if (Widgets::BeginSettingsCard("##preset-memory-width",
						IA(I18nKey.SettingsUI.Preset.Memory.Thickness).c_str(),
						IA(I18nKey.SettingsUI.Preset.Memory.ThicknessE).c_str(), "\ue9a6"))
					{
						ImFluent::ToggleSwitch("##preset-memory-width-toggle",
							&PresetSetting.MemoryWidth, "", "");
						if (setlist.presetSetting.memoryWidth != PresetSetting.MemoryWidth)
						{
							setlist.presetSetting.memoryWidth = PresetSetting.MemoryWidth;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					if (Widgets::BeginSettingsCard("##preset-memory-color",
						IA(I18nKey.SettingsUI.Preset.Memory.Color).c_str(),
						IA(I18nKey.SettingsUI.Preset.Memory.ColorE).c_str(), "\ue790"))
					{
						ImFluent::ToggleSwitch("##preset-memory-color-toggle",
							&PresetSetting.MemoryColor, "", "");
						if (setlist.presetSetting.memoryColor != PresetSetting.MemoryColor)
						{
							setlist.presetSetting.memoryColor = PresetSetting.MemoryColor;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader(IA(I18nKey.SettingsUI.Preset.Preset.N).c_str());
					const int brushPreset = static_cast<int>(stateMode.Pen.Brush1.widthPreset);
					const int highlighterPreset =
						static_cast<int>(stateMode.Pen.Highlighter1.widthPreset);
					const string adaptiveDescription = vformat(
						IA(I18nKey.SettingsUI.Preset.Preset.AutoThicknessE),
						make_format_args(brushPreset, highlighterPreset));
					if (Widgets::BeginSettingsCard("##preset-adaptive",
						IA(I18nKey.SettingsUI.Preset.Preset.AutoThickness).c_str(),
						adaptiveDescription.c_str(), "\ue8d7"))
					{
						ImFluent::ToggleSwitch("##preset-adaptive-toggle",
							&PresetSetting.AutoDefaultWidth, "", "");
						if (setlist.presetSetting.autoDefaultWidth != PresetSetting.AutoDefaultWidth)
						{
							setlist.presetSetting.autoDefaultWidth = PresetSetting.AutoDefaultWidth;
							WriteSetting();
						}
						Widgets::EndSettingsCard();
					}
					if (!PresetSetting.AutoDefaultWidth)
					{
						if (Widgets::BeginSettingsCard("##preset-pen-width",
							IA(I18nKey.SettingsUI.Preset.Preset.Pen).c_str(),
							nullptr, "\uee56"))
						{
							ImFluent::Slider("##preset-pen-width-slider",
								&PresetSetting.DefaultBrush1Width, 1.0F, 30.0F, "%.0f");
							PresetSetting.DefaultBrush1Width =
								round(PresetSetting.DefaultBrush1Width);
							if (!ImGui::IsItemActive()
								&& setlist.presetSetting.defaultBrush1Width
									!= PresetSetting.DefaultBrush1Width)
							{
								setlist.presetSetting.defaultBrush1Width =
									PresetSetting.DefaultBrush1Width;
								WriteSetting();
							}
							Widgets::EndSettingsCard();
						}
						if (Widgets::BeginSettingsCard("##preset-highlighter-width",
							IA(I18nKey.SettingsUI.Preset.Preset.Highlighter).c_str(),
							nullptr, "\ue7e6"))
						{
							ImFluent::Slider("##preset-highlighter-width-slider",
								&PresetSetting.DefaultHighlighter1Width,
								10.0F, 100.0F, "%.0f");
							PresetSetting.DefaultHighlighter1Width =
								round(PresetSetting.DefaultHighlighter1Width);
							if (!ImGui::IsItemActive()
								&& setlist.presetSetting.defaultHighlighter1Width
									!= PresetSetting.DefaultHighlighter1Width)
							{
								setlist.presetSetting.defaultHighlighter1Width =
									PresetSetting.DefaultHighlighter1Width;
								WriteSetting();
							}
							Widgets::EndSettingsCard();
						}
					}
					break;
				}
				case settingTabEnum::tab4:
				{
					const auto renderPluginHeader = [&](const char* title,
						const char* subtitle, const wchar_t* sourceUrl = nullptr)
						{
							if (ImFluent::Button("\ue72b##plugin-back",
								{ 36.0F * settingGlobalScale, 36.0F * settingGlobalScale }))
								settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							ImGui::SameLine(0.0F, 12.0F * settingGlobalScale);
							Widgets::PageHeader(title, subtitle);
							if (sourceUrl)
							{
								if (ImFluent::HyperlinkButton("GitHub"))
									ShellExecuteW(nullptr, nullptr, sourceUrl, nullptr, nullptr, SW_SHOW);
							}
							Widgets::PageContentStart();
						};
					const auto renderPluginOverviewCard = [&](const char* id,
						const char* title, const string& status, const char* description,
						const char* glyph, int target)
						{
							string overviewDescription = status;
							if (!overviewDescription.empty() && description && *description)
								overviewDescription += "\n";
							if (description) overviewDescription += description;
							if (Widgets::BeginSettingsCard(id, title,
								overviewDescription.c_str(), glyph))
							{
								if (ImFluent::Button("插件选项")) settingPlugInTab = target;
								Widgets::EndSettingsCard();
							}
						};
					const auto renderToggleCard = [&](const char* id, const char* title,
						const char* description, const char* glyph, bool& value, auto&& changed)
						{
							if (!Widgets::BeginSettingsCard(id, title, description, glyph)) return;
							const bool previous = value;
							ImFluent::ToggleSwitch("##toggle", &value, "", "");
							if (previous != value) changed(value);
							Widgets::EndSettingsCard();
						};

					switch (settingPlugInTab)
					{
					case settingPlugInTabEnum::tabPlug1:
					{
						Widgets::PageHeader(IA(I18nKey.SettingsUI.PlugIn.N).c_str());
						Widgets::PageContentStart();
						const string pptStatus = pptComVersion.substr(0, 7) == L"Error: "
							? IA(I18nKey.SettingsUI.PlugIn.PPTHelper.VersionError)
							: utf16ToUtf8(pptComVersion);
						renderPluginOverviewCard("ppt-helper",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str(), pptStatus,
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.E).c_str(), "\ue8a5",
							settingPlugInTabEnum::tabPlug2);
						renderPluginOverviewCard("super-top",
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.N).c_str(), "20260202a",
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.E).c_str(), "\ue922",
							settingPlugInTabEnum::tabSuperTop);
						renderPluginOverviewCard("ddb",
							IA(I18nKey.SettingsUI.PlugIn.DesktopDrawpadBlocker.N).c_str(),
							utf16ToUtf8(ddbInteractionSetList.DdbEdition),
							IA(I18nKey.SettingsUI.PlugIn.DesktopDrawpadBlocker.E).c_str(), "\ue7ba",
							settingPlugInTabEnum::tabPlugDDB);
						renderPluginOverviewCard("shortcut-helper",
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.N).c_str(), "20250223a",
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.E).c_str(), "\ue71b",
							settingPlugInTabEnum::tabPlug3);
						break;
					}

					case settingPlugInTabEnum::tabPlug2:
					{
						const string pptVersionText = pptComVersion.substr(0, 7) == L"Error: "
							? IA(I18nKey.SettingsUI.PlugIn.PPTHelper.VersionError)
							: utf16ToUtf8(pptComVersion);
						renderPluginHeader(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str(),
							pptVersionText.c_str());
						if (pptComVersion.substr(0, 7) == L"Error: ")
						{
							const string error = IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Error)
								+ utf16ToUtf8(pptComVersion);
							ImFluent::InfoBar(ImFluentInfoSeverity_Warning,
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.VersionError).c_str(),
								error.c_str());
						}
						if (ImFluent::InfoBar(ImFluentInfoSeverity_Informational,
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tip).c_str(),
							nullptr, nullptr, true,
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Solve).c_str()))
							ShellExecuteW(nullptr, nullptr, L"https://www.inkeys.top/tutorial/ppt-com",
								nullptr, nullptr, SW_SHOW);
						if (pptComSetlist.setAdmin)
						{
							if (ImFluent::InfoBar(ImFluentInfoSeverity_Warning,
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str(),
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Warn).c_str(),
								nullptr, nullptr, true,
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Solve).c_str()))
								ShellExecuteW(nullptr, nullptr, L"https://www.inkeys.top/tutorial/ppt-admin",
									nullptr, nullptr, SW_SHOW);
						}

						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.N).c_str());
						renderToggleCard("##ppt-fixed-ink",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.InkFixation).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.InkFixationE).c_str(),
							"\ue7c3", PptComFixedHandWriting, [&](bool value)
							{
								pptComSetlist.fixedHandWriting = value;
								PptComWriteSetting();
							});
						renderToggleCard("##ppt-loading-screen",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.LoadPage).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.LoadPageE).c_str(),
							"\ue895", PptComShowLoadingScreen, [&](bool value)
							{
								pptComSetlist.showLoadingScreen = value;
								PptComWriteSetting();
							});

						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.N).c_str());
						renderToggleCard("##ppt-bottom-pair",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomBoth).c_str(),
							nullptr, "\ue903", ShowBottomBoth, [&](bool value)
							{
								pptComSetlist.showBottomBoth = value;
								PptComWriteSetting();
								Inkeys::UI::Ppt::NotifyConfigurationChanged(
									Inkeys::UI::Ppt::ConfigGroup::BottomPair);
							});
						renderToggleCard("##ppt-middle-pair",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.MiddleBoth).c_str(),
							nullptr, "\ue903", ShowMiddleBoth, [&](bool value)
							{
								pptComSetlist.showMiddleBoth = value;
								PptComWriteSetting();
								Inkeys::UI::Ppt::NotifyConfigurationChanged(
									Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
							});
						renderToggleCard("##ppt-exit-control",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomMiddle).c_str(),
							nullptr, "\ue903", ShowBottomMiddle, [&](bool value)
							{
								pptComSetlist.showBottomMiddle = value;
								PptComWriteSetting();
								Inkeys::UI::Ppt::NotifyConfigurationChanged(
									Inkeys::UI::Ppt::ConfigGroup::ExitShow);
							});

						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.N).c_str());
						if (Widgets::BeginSettingsCard("##ppt-reset-position",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.Reset).c_str(),
							nullptr, "\ue777"))
						{
							if (ImFluent::Button(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset).c_str()))
							{
								pptComSetlist.bottomBothWidth = BottomBothWidth = 0;
								pptComSetlist.bottomBothHeight = BottomBothHeight = 0;
								pptComSetlist.middleBothWidth = MiddleBothWidth = 0;
								pptComSetlist.middleBothHeight = MiddleBothHeight = 0;
								pptComSetlist.bottomMiddleWidth = BottomMiddleWidth = 0;
								pptComSetlist.bottomMiddleHeight = BottomMiddleHeight = 0;
								PptComWriteSetting();
								Inkeys::UI::Ppt::NotifyConfigurationChanged(
									Inkeys::UI::Ppt::ConfigGroup::All);
							}
							Widgets::EndSettingsCard();
						}
						renderToggleCard("##ppt-remember-position",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.Remember).c_str(),
							nullptr, "\ue8f1", MemoryWidgetPosition, [&](bool value)
							{
								pptComSetlist.memoryWidgetPosition = value;
								PptComWriteSetting();
							});

						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.N).c_str());
						bool anyScaleActive = false;
						bool bottomSyncEnabled = false;
						bool middleSyncEnabled = false;
						bool exitSyncEnabled = false;
						const auto renderScaleCard = [&](const char* id, const char* title,
							float& value, bool& unified, bool& syncEnabled)
							{
								if (!ImFluent::BeginCard(id, { 0.0F, 0.0F }, ImFluentCardStyle_Filled))
								{
									ImFluent::EndCard();
									return;
								}
								ImFluent::TextBlock(title, ImFluentTextStyle_BodyStrong);
								ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
								ImFluent::Slider("##scale", &value, 0.5F, 3.0F, "%.2f");
								value = round(value * 100.0F) / 100.0F;
								anyScaleActive = anyScaleActive || ImGui::IsItemActive();
								const string indicator = vformat(
									IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind),
									make_format_args(value));
								ImFluent::TextBlockColored(indicator.c_str(),
									Widgets::Color(ImFluentCol_TextSecondary),
									ImFluentTextStyle_Caption);
								ImFluent::BeginWrapPanel();
								ImFluent::WrapPanelNextItem(110.0F * settingGlobalScale);
								if (ImFluent::Button(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset).c_str(),
									{ 110.0F * settingGlobalScale, 0.0F }))
									value = 1.0F;
								ImFluent::WrapPanelNextItem(110.0F * settingGlobalScale);
								const bool previousUnified = unified;
								ImFluent::ToggleButton(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Sync).c_str(),
									&unified, { 110.0F * settingGlobalScale, 0.0F });
								syncEnabled = !previousUnified && unified;
								ImFluent::EndWrapPanel();
								ImFluent::EndCard();
							};
						renderScaleCard("##ppt-bottom-scale",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.BottomSideBoth).c_str(),
							BottomSideBothWidgetScale, BottomSideBothWidgetScaleUnifie,
							bottomSyncEnabled);
						renderScaleCard("##ppt-middle-scale",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.MiddleSideBoth).c_str(),
							MiddleSideBothWidgetScale, MiddleSideBothWidgetScaleUnifie,
							middleSyncEnabled);
						renderScaleCard("##ppt-exit-scale",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.State.BottomSideMiddle).c_str(),
							BottomSideMiddleWidgetScale, BottomSideMiddleWidgetScaleUnifie,
							exitSyncEnabled);

						if (bottomSyncEnabled)
						{
							if (MiddleSideBothWidgetScaleUnifie)
								BottomSideBothWidgetScale = MiddleSideBothWidgetScale;
							else if (BottomSideMiddleWidgetScaleUnifie)
								BottomSideBothWidgetScale = BottomSideMiddleWidgetScale;
						}
						if (middleSyncEnabled)
						{
							if (BottomSideBothWidgetScaleUnifie)
								MiddleSideBothWidgetScale = BottomSideBothWidgetScale;
							else if (BottomSideMiddleWidgetScaleUnifie)
								MiddleSideBothWidgetScale = BottomSideMiddleWidgetScale;
						}
						if (exitSyncEnabled)
						{
							if (BottomSideBothWidgetScaleUnifie)
								BottomSideMiddleWidgetScale = BottomSideBothWidgetScale;
							else if (MiddleSideBothWidgetScaleUnifie)
								BottomSideMiddleWidgetScale = MiddleSideBothWidgetScale;
						}

						const bool bottomChanged = BottomSideBothWidgetScale
							!= pptComSetlist.bottomSideBothWidgetScale;
						const bool middleChanged = MiddleSideBothWidgetScale
							!= pptComSetlist.middleSideBothWidgetScale;
						const bool exitChanged = BottomSideMiddleWidgetScale
							!= pptComSetlist.bottomSideMiddleWidgetScale;
						if (bottomChanged && BottomSideBothWidgetScaleUnifie)
						{
							if (MiddleSideBothWidgetScaleUnifie)
								MiddleSideBothWidgetScale = BottomSideBothWidgetScale;
							if (BottomSideMiddleWidgetScaleUnifie)
								BottomSideMiddleWidgetScale = BottomSideBothWidgetScale;
						}
						else if (middleChanged && MiddleSideBothWidgetScaleUnifie)
						{
							if (BottomSideBothWidgetScaleUnifie)
								BottomSideBothWidgetScale = MiddleSideBothWidgetScale;
							if (BottomSideMiddleWidgetScaleUnifie)
								BottomSideMiddleWidgetScale = MiddleSideBothWidgetScale;
						}
						else if (exitChanged && BottomSideMiddleWidgetScaleUnifie)
						{
							if (BottomSideBothWidgetScaleUnifie)
								BottomSideBothWidgetScale = BottomSideMiddleWidgetScale;
							if (MiddleSideBothWidgetScaleUnifie)
								MiddleSideBothWidgetScale = BottomSideMiddleWidgetScale;
						}

						const bool scaleValuesChanged =
							pptComSetlist.bottomSideBothWidgetScale != BottomSideBothWidgetScale
							|| pptComSetlist.middleSideBothWidgetScale != MiddleSideBothWidgetScale
							|| pptComSetlist.bottomSideMiddleWidgetScale != BottomSideMiddleWidgetScale;
						if (scaleValuesChanged)
						{
							pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
							pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
							pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale;
							Inkeys::UI::Ppt::NotifyConfigurationChanged(
								Inkeys::UI::Ppt::ConfigGroup::All);
						}
						if (!anyScaleActive
							&& (BottomSideBothWidgetScaleRecord != BottomSideBothWidgetScale
								|| MiddleSideBothWidgetScaleRecord != MiddleSideBothWidgetScale
								|| BottomSideMiddleWidgetScaleRecord != BottomSideMiddleWidgetScale))
						{
							BottomSideBothWidgetScaleRecord = BottomSideBothWidgetScale;
							MiddleSideBothWidgetScaleRecord = MiddleSideBothWidgetScale;
							BottomSideMiddleWidgetScaleRecord = BottomSideMiddleWidgetScale;
							PptComWriteSetting();
						}

						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.N).c_str());
						bool autoTakeOver = Inkeys::config.PlugIn.PPTHelper.AutoTakeOver;
						renderToggleCard("##ppt-auto-takeover",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOver).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverE).c_str(),
							"\ue7ba", autoTakeOver, [&](bool value)
							{
								Inkeys::config.PlugIn.PPTHelper.AutoTakeOver = value;
								QueueConfigWrite();
							});
						bool autoTakeOverOnce = Inkeys::config.PlugIn.PPTHelper.AutoTakeOverOnce;
						renderToggleCard("##ppt-auto-takeover-once",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverOnce).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverOnceE).c_str(),
							"\ue7ba", autoTakeOverOnce, [&](bool value)
							{
								Inkeys::config.PlugIn.PPTHelper.AutoTakeOverOnce = value;
								QueueConfigWrite();
							});
						bool autoTakeOverExpand = Inkeys::config.PlugIn.PPTHelper.AutoTakeOverExpand;
						renderToggleCard("##ppt-auto-takeover-expand",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverExpand).c_str(),
							nullptr, "\ue7ba", autoTakeOverExpand, [&](bool value)
							{
								Inkeys::config.PlugIn.PPTHelper.AutoTakeOverExpand = value;
								QueueConfigWrite();
							});
						bool enableLongPress =
							Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress;
						renderToggleCard("##ppt-long-press",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress).c_str(),
							nullptr, "\ue7ba", enableLongPress, [&](bool value)
							{
								Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress = value;
								QueueConfigWrite();
							});
						break;
					}

					case settingPlugInTabEnum::tabSuperTop:
					{
						renderPluginHeader(IA(I18nKey.SettingsUI.PlugIn.SuperTop.N).c_str(),
							"20260202a");
						ImFluent::InfoBar(ImFluentInfoSeverity_Warning,
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.N).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.Warn).c_str());
						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.N).c_str());
						const string superTopDescription = hasSuperTop
							? IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.SuperTopE1)
							: IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.SuperTopE2);
						renderToggleCard("##super-top-enable",
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.SuperTop).c_str(),
							superTopDescription.c_str(), "\ue922",
							PlugInSetting.SuperTop.Enable, [&](bool value)
							{
								setlist.plugInSetting.superTop.enable = value;
								WriteSetting();
							});
						renderToggleCard("##super-top-indicator",
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.Indicator).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.IndicatorE).c_str(),
							"\ue7ba", PlugInSetting.SuperTop.Indicator, [&](bool value)
							{
								setlist.plugInSetting.superTop.indicator = value;
								WriteSetting();
							});
						break;
					}

					case settingPlugInTabEnum::tabPlug3:
					{
						renderPluginHeader(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.N).c_str(),
							"20250223a");
						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.N).c_str());
						renderToggleCard("##shortcut-correct",
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.FixLnk).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.FixLnkE).c_str(),
							"\ue71b", CorrectLnk, [&](bool value)
							{
								setlist.shortcutAssistant.correctLnk = value;
								WriteSetting();
								if (value) shortcutAssistant.SetShortcut();
							});
						Widgets::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.N).c_str());
						renderToggleCard("##shortcut-create",
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnk).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnkE).c_str(),
							"\ue71b", CreateLnk, [&](bool value)
							{
								setlist.shortcutAssistant.createLnk = value;
								WriteSetting();
								if (setlist.shortcutAssistant.correctLnk)
									shortcutAssistant.SetShortcut();
							});
						break;
					}

					case settingPlugInTabEnum::tabPlugDDB:
					{
						renderPluginHeader(
							IA(I18nKey.SettingsUI.PlugIn.DesktopDrawpadBlocker.N).c_str(),
							utf16ToUtf8(ddbInteractionSetList.DdbEdition).c_str(),
							L"https://github.com/Alan-CRL/DesktopDrawpadBlocker");
						ImFluent::InfoBar(ImFluentInfoSeverity_Warning,
							"GPLv3 开源插件",
							"插件仅供学习、交流和研究使用。使用 DesktopDrawpadBlocker 即视为同意其许可与风险说明。");

						Widgets::SectionHeader("使用插件");
						renderToggleCard("##ddb-enable", "启用插件",
							"默认模式下插件随软件开启和关闭。", "\ue7ba",
							Ddb.Enable, [&](bool value)
							{
								ddbInteractionSetList.enable = value;
								SettingBusinessCommand command;
								command.kind = SettingBusinessKind::ConfigureDdb;
								command.flag = value;
								command.secondaryFlag = ddbInteractionSetList.runAsAdmin;
								command.text = pluginPath
									+ L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe";
								command.directory = pluginPath + L"DesktopDrawpadBlocker";
								command.parameters = GetCurrentExePath();
								command.digest = ddbInteractionSetList.DdbSHA256;
								QueueBusiness(std::move(command));
							});

						Widgets::SectionHeader("常规选项");
						renderToggleCard("##ddb-admin", "以管理员身份启动插件",
							"切换后软件将等待插件响应，最长等待时间由拦截间隔决定。", "\ue72e",
							Ddb.RunAsAdmin, [&](bool value)
							{
								ddbInteractionSetList.runAsAdmin = value;
								WriteSetting();
								SettingBusinessCommand command;
								command.kind = SettingBusinessKind::RestartDdb;
								command.flag = value;
								command.text = pluginPath
									+ L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe";
								QueueBusiness(std::move(command));
							});
						if (Widgets::BeginSettingsCard("##ddb-interval", "拦截间隔",
							"更短的间隔可以更快拦截窗口，但会占用更多 CPU。", "\ue916"))
						{
							int intervalIndex = ddbInteractionSetList.sleepTime == 500 ? 0
								: (ddbInteractionSetList.sleepTime == 1000 ? 1
									: (ddbInteractionSetList.sleepTime == 3000 ? 2
										: (ddbInteractionSetList.sleepTime == 10000 ? 4 : 3)));
							const int previousIntervalIndex = intervalIndex;
							vector<string> intervals{
								"短（500ms）", "较短（1s）", "中等（3s）", "较长（5s）", "长（10s）"
							};
							if (Widgets::combo.Select("##ddb-interval-select", &intervalIndex, intervals)
								&& previousIntervalIndex != intervalIndex)
							{
								if (intervalIndex == 0) ddbInteractionSetList.sleepTime = 500;
								else if (intervalIndex == 1) ddbInteractionSetList.sleepTime = 1000;
								else if (intervalIndex == 2) ddbInteractionSetList.sleepTime = 3000;
								else if (intervalIndex == 4) ddbInteractionSetList.sleepTime = 10000;
								else ddbInteractionSetList.sleepTime = 5000;
								WriteSetting();
								QueueDdbWriteInteraction(true, false);
							}
							Widgets::EndSettingsCard();
						}

						Widgets::SectionHeader("精确控制");
						struct DdbInterceptRow
						{
							const char* id;
							const char* title;
							const char* description;
							bool* value;
						};
						const DdbInterceptRow interceptRows[] = {
							{ "##ddb-seewo3", "希沃白板3 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.SeewoWhiteboard3Floating },
							{ "##ddb-seewo5", "希沃白板5 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.SeewoWhiteboard5Floating },
							{ "##ddb-seewo5c", "希沃轻白板（5C）桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.SeewoWhiteboard5CFloating },
							{ "##ddb-pinco-sidebar", "希沃品课教师端 侧栏悬浮窗", nullptr,
								&Ddb.intercept.SeewoPincoSideBarFloating },
							{ "##ddb-pinco-drawing", "希沃品课教师端 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.SeewoPincoDrawingFloating },
							{ "##ddb-seewo-ppt", "希沃 PPT 小工具", nullptr,
								&Ddb.intercept.SeewoPPTFloating },
							{ "##ddb-iwb-assistant", "希沃课堂助手 PPT 小工具", nullptr,
								&Ddb.intercept.SeewoIwbAssistantFloating },
							{ "##ddb-yiou", "欧帝白板 桌面画笔悬浮窗", "支持自动恢复。",
								&Ddb.intercept.YiouBoardFloating },
							{ "##ddb-aiclass", "AiClass 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.AiClassFloating },
							{ "##ddb-classinx", "ClassIn X 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.ClassInXFloating },
							{ "##ddb-intelligent-class", "天喻教育云互动课堂 桌面画笔悬浮窗",
								"包括 PPT 控件。", &Ddb.intercept.IntelligentClassFloating },
							{ "##ddb-changyan4", "畅言智慧课堂 4.0 桌面画笔悬浮窗",
								"包括 PPT 控件，支持白板自动恢复，需要管理员权限。",
								&Ddb.intercept.ChangYanFloating },
							{ "##ddb-changyan5", "畅言智慧课堂 5.0 桌面画笔悬浮窗",
								"包括 PPT 控件，支持白板自动恢复，需要管理员权限。",
								&Ddb.intercept.ChangYan5Floating },
							{ "##ddb-iclass30-sidebar", "C30 智能教学 侧栏悬浮窗",
								"需要管理员权限。", &Ddb.intercept.Iclass30SidebarFloating },
							{ "##ddb-iclass30-drawing", "C30 智能教学 桌面画笔悬浮窗",
								"包括 PPT 控件，支持白板恢复和窗口追踪，需要管理员权限。",
								&Ddb.intercept.Iclass30Floating },
							{ "##ddb-seewo-desktop-sidebar", "希沃桌面 侧栏悬浮窗",
								"1.0/2.0/2.5/3.0 普教版与高教版通用，需要管理员权限。",
								&Ddb.intercept.SeewoDesktopSideBarFloating },
							{ "##ddb-seewo-desktop-drawing", "希沃桌面 桌面画笔悬浮窗",
								"1.0/2.0/2.5/3.0 普教版与高教版通用，需要管理员权限。",
								&Ddb.intercept.SeewoDesktopDrawingFloating }
						};
						bool interceptChanged = false;
						for (const auto& row : interceptRows)
						{
							if (!Widgets::BeginSettingsCard(row.id, row.title,
								row.description, "\ue7ba"))
								continue;
							const bool previous = *row.value;
							ImFluent::ToggleSwitch("##toggle", row.value, "", "");
							if (previous != *row.value)
								interceptChanged = true;
							Widgets::EndSettingsCard();
						}
						if (interceptChanged)
						{
							auto& stored = ddbInteractionSetList.intercept;
							stored.SeewoWhiteboard3Floating = Ddb.intercept.SeewoWhiteboard3Floating;
							stored.SeewoWhiteboard5Floating = Ddb.intercept.SeewoWhiteboard5Floating;
							stored.SeewoWhiteboard5CFloating = Ddb.intercept.SeewoWhiteboard5CFloating;
							stored.SeewoPincoSideBarFloating = Ddb.intercept.SeewoPincoSideBarFloating;
							stored.SeewoPincoDrawingFloating = Ddb.intercept.SeewoPincoDrawingFloating;
							stored.SeewoPPTFloating = Ddb.intercept.SeewoPPTFloating;
							stored.SeewoIwbAssistantFloating = Ddb.intercept.SeewoIwbAssistantFloating;
							stored.YiouBoardFloating = Ddb.intercept.YiouBoardFloating;
							stored.AiClassFloating = Ddb.intercept.AiClassFloating;
							stored.ClassInXFloating = Ddb.intercept.ClassInXFloating;
							stored.IntelligentClassFloating = Ddb.intercept.IntelligentClassFloating;
							stored.ChangYanFloating = Ddb.intercept.ChangYanFloating;
							stored.ChangYan5Floating = Ddb.intercept.ChangYan5Floating;
							stored.Iclass30SidebarFloating = Ddb.intercept.Iclass30SidebarFloating;
							stored.Iclass30Floating = Ddb.intercept.Iclass30Floating;
							stored.SeewoDesktopSideBarFloating = Ddb.intercept.SeewoDesktopSideBarFloating;
							stored.SeewoDesktopDrawingFloating = Ddb.intercept.SeewoDesktopDrawingFloating;
							WriteSetting();
							QueueDdbWriteInteraction(true, false);
						}
						break;
					}
					}
					break;
				}

				case settingTabEnum::tabComponent:
				{
					Widgets::PageHeader("组件");
					auto renderComponentToggle = [&](const char* id, const char* title,
						const char* description, const char* glyph, bool& value, auto& stored)
					{
						if (!Widgets::BeginSettingsCard(id, title, description, glyph)) return;
						const string toggleId = string("##toggle-") + id;
						ImFluent::ToggleSwitch(toggleId.c_str(), &value, "", "");
						if (stored != value)
						{
							stored = value;
							WriteSetting();
							SyncUi3BuiltInComponents();
						}
						Widgets::EndSettingsCard();
					};
					auto componentSection = [&](const char* title)
					{
						Widgets::SectionHeader(title);
					};

					componentSection("软件");
					renderComponentToggle("component-explorer", "启动 文件资源管理器", nullptr,
						"\ue838", ComponentShortcutButtonApplianceExplorer,
						setlist.component.shortcutButton.appliance.explorer);
					renderComponentToggle("component-taskmgr", "启动 任务管理器", nullptr,
						"\ue9d9", ComponentShortcutButtonApplianceTaskmgr,
						setlist.component.shortcutButton.appliance.taskmgr);
					renderComponentToggle("component-control", "启动 控制面板", nullptr,
						"\ue713", ComponentShortcutButtonApplianceControl,
						setlist.component.shortcutButton.appliance.control);

					componentSection("系统");
					renderComponentToggle("component-desktop", "显示 桌面", nullptr,
						"\uea14", ComponentShortcutButtonSystemDesktop,
						setlist.component.shortcutButton.system.desktop);
					renderComponentToggle("component-lock", "锁屏", nullptr,
						"\ue72e", ComponentShortcutButtonSystemLockWorkStation,
						setlist.component.shortcutButton.system.lockWorkStation);

					componentSection("键盘模拟");
					renderComponentToggle("component-escape", "按下 ESC", nullptr,
						"\ue765", ComponentShortcutButtonKeyboardKeyboardesc,
						setlist.component.shortcutButton.keyboard.keyboardesc);
					renderComponentToggle("component-alt-f4", "按下 Alt+F4", nullptr,
						"\ue765", ComponentShortcutButtonKeyboardKeyboardAltF4,
						setlist.component.shortcutButton.keyboard.keyboardAltF4);

					componentSection("点名器");
					renderComponentToggle("component-island-caller-1",
						"IslandCaller 1 随机点名",
						"需要安装 ClassIsland 插件 IslandCaller，并注册 Url 协议。",
						"\ue716", ComponentShortcutButtonRollCallIslandCaller1,
						setlist.component.shortcutButton.rollCall.IslandCaller1);
					renderComponentToggle("component-island-caller-2",
						"IslandCaller 2 单人点名",
						"需要安装 ClassIsland 插件 IslandCaller，并注册 Url 协议。",
						"\ue716", ComponentShortcutButtonRollCallIslandCaller2,
						setlist.component.shortcutButton.rollCall.IslandCaller2);
					renderComponentToggle("component-sec-random-1", "SecRandom 1 闪抽",
						"需要 SecRandom 注册 Url 协议。", "\ue716",
						ComponentShortcutButtonRollCallSecRandom1,
						setlist.component.shortcutButton.rollCall.SecRandom1);
					renderComponentToggle("component-sec-random-2", "SecRandom 2 闪抽",
						"需要 SecRandom 注册 Ipc 协议。", "\ue716",
						ComponentShortcutButtonRollCallSecRandom2,
						setlist.component.shortcutButton.rollCall.SecRandom2);
					renderComponentToggle("component-sec-random-compat",
						"SecRandom 2 闪抽（兼容）", "需要 SecRandom 注册 Url 协议。", "\ue716",
						ComponentShortcutButtonRollCallSecRandom2Compat,
						setlist.component.shortcutButton.rollCall.SecRandom2Compat);
					renderComponentToggle("component-name-picker", "NamePicker 随机点名",
						"需要 NamePicker 注册 Url 协议。", "\ue716",
						ComponentShortcutButtonRollCallNamePicker,
						setlist.component.shortcutButton.rollCall.NamePicker);

					componentSection("ClassIsland 联动");
					ImFluent::InfoBar(ImFluentInfoSeverity_Informational, "ClassIsland",
						"需要在 ClassIsland 应用设置中注册 Url 导航协议。");
					renderComponentToggle("component-classisland-settings",
						"ClassIsland 应用设置", nullptr, "\ue713",
						ComponentShortcutButtonLinkageClassislandSettings,
						setlist.component.shortcutButton.linkage.classislandSettings);
					renderComponentToggle("component-classisland-profile",
						"ClassIsland 档案编辑", nullptr, "\ue70f",
						ComponentShortcutButtonLinkageClassislandProfile,
						setlist.component.shortcutButton.linkage.classislandProfile);
					renderComponentToggle("component-classisland-swap",
						"ClassIsland 快速换课",
						"ClassIsland 当前没有加载课表时，此组件不起作用。", "\ue8d7",
						ComponentShortcutButtonLinkageClassislandClassswap,
						setlist.component.shortcutButton.linkage.classislandClassswap);
					break;
				}
				case settingTabEnum::tab5:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.HotKey.N).c_str());
					Widgets::PageContentStart();
					const string hotKeyDescription =
						utf16ToUtf8(IW(I18nKey.SettingsUI.HotKey.E));
					ImFluent::InfoBar(ImFluentInfoSeverity_Informational,
						IA(I18nKey.SettingsUI.HotKey.N).c_str(),
						hotKeyDescription.c_str(), nullptr, nullptr, true);
					break;
				}
				case settingTabEnum::tabExperimental:
				{
					Widgets::PageHeader("实验室");
					Widgets::SectionHeader("Inkeys3");

					if (Experimental.Inkeys3.EdgeLightingEnable
						&& Widgets::BeginSettingsCard("##experimental-dynamic-light",
							"动态边缘光影",
							"控制跟随鼠标的第三光源，关闭后停止全局鼠标跟踪。", "\ue706"))
					{
						ImFluent::ToggleSwitch("##experimental-dynamic-light-toggle",
							&Experimental.Inkeys3.DynamicEdgeLighting, "", "");
						if (Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic
							!= Experimental.Inkeys3.DynamicEdgeLighting)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic =
								Experimental.Inkeys3.DynamicEdgeLighting;
							Inkeys::UI::Bar::SetEdgeLightingOptions(
								Experimental.Inkeys3.EdgeLightingEnable,
								Experimental.Inkeys3.DynamicEdgeLighting);
							QueueConfigWrite();
						}
						Widgets::EndSettingsCard();
					}

					if (Widgets::BeginSettingsCard("##experimental-dirty-debug",
						"脏区调试", "显示 UI3 每帧实际提交的脏区边界。", "\ue90f"))
					{
						ImFluent::ToggleSwitch("##experimental-dirty-debug-toggle",
							&Experimental.Inkeys3.DebugMode, "", "");
						if (Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable
							!= Experimental.Inkeys3.DebugMode)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable =
								Experimental.Inkeys3.DebugMode;
							Inkeys::UI::Bar::SetDebugOptions(
								Experimental.Inkeys3.DebugMode,
								Experimental.Inkeys3.ShowFrameRate);
							QueueConfigWrite();
						}
						Widgets::EndSettingsCard();
					}

					if (Experimental.Inkeys3.DebugMode
						&& Widgets::BeginSettingsCard("##experimental-frame-rate",
							"显示帧率", "每秒更新上一秒平均帧率和无等待帧率。", "\ue9d9"))
					{
						ImFluent::ToggleSwitch("##experimental-frame-rate-toggle",
							&Experimental.Inkeys3.ShowFrameRate, "", "");
						if (Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate
							!= Experimental.Inkeys3.ShowFrameRate)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate =
								Experimental.Inkeys3.ShowFrameRate;
							Inkeys::UI::Bar::SetDebugOptions(
								Experimental.Inkeys3.DebugMode,
								Experimental.Inkeys3.ShowFrameRate);
							QueueConfigWrite();
						}
						Widgets::EndSettingsCard();
					}

					if (Widgets::BeginSettingsCard("##experimental-animation",
						"启用动画", "关闭后，UI3 主栏动画将立即完成。", "\ue945"))
					{
						ImFluent::ToggleSwitch("##experimental-animation-toggle",
							&Experimental.Inkeys3.AnimationEnable, "", "");
						if (Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable
							!= Experimental.Inkeys3.AnimationEnable)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable =
								Experimental.Inkeys3.AnimationEnable;
							Inkeys::UI::Bar::SetAnimationOptions(
								Experimental.Inkeys3.AnimationEnable,
								Experimental.Inkeys3.AnimationSpeedRate);
							QueueConfigWrite();
						}
						Widgets::EndSettingsCard();
					}

					if (Widgets::BeginSettingsCard("##experimental-animation-speed",
						"动画速度", "调整 UI3 主栏动画速度，范围为 0.1x-5.0x。", "\ue9a6"))
					{
						ImFluent::Slider("##experimental-animation-speed-slider",
							&Experimental.Inkeys3.AnimationSpeedRate, 0.1F, 5.0F, "%.1fx");
						Experimental.Inkeys3.AnimationSpeedRate =
							round(Experimental.Inkeys3.AnimationSpeedRate * 10.0F) / 10.0F;
						const bool isItemActive = ImGui::IsItemActive();
						if (fabs(Experimental.Inkeys3.AnimationSpeedRate - static_cast<float>(
							Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate.load())) > 0.0001F)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate =
								static_cast<double>(Experimental.Inkeys3.AnimationSpeedRate);
							Inkeys::UI::Bar::SetAnimationOptions(
								Experimental.Inkeys3.AnimationEnable,
								Experimental.Inkeys3.AnimationSpeedRate);
							Experimental.Inkeys3.AnimationSpeedSavePending = true;
						}
						if (!isItemActive && Experimental.Inkeys3.AnimationSpeedSavePending)
						{
							QueueConfigWrite();
							Experimental.Inkeys3.AnimationSpeedSavePending = false;
						}
						Widgets::EndSettingsCard();
					}
					break;
				}
				case settingTabEnum::tab8:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.Sponsor.N).c_str());
					Widgets::PageContentStart();
					const float sponsorWidth = min(ImGui::GetContentRegionAvail().x,
						700.0F * settingGlobalScale);
					const float sponsorAspect = settingSign[9].width > 0
						? static_cast<float>(settingSign[9].height)
							/ static_cast<float>(settingSign[9].width)
						: 0.56F;
					const bool sponsorCardVisible = ImFluent::BeginCard("##sponsor-card",
						{ sponsorWidth, 0.0F }, ImFluentCardStyle_Filled);
					if (sponsorCardVisible)
					{
						const float imageWidth = ImGui::GetContentRegionAvail().x;
						ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[9],
							{ imageWidth, imageWidth * sponsorAspect });
						ImFluent::TextBlockColored(
							"成功赞助后，可以联系作者将您的昵称和赞助金额添加到社区名片中。",
							Widgets::Color(ImFluentCol_TextSecondary), ImFluentTextStyle_Body);
					}
					ImFluent::EndCard();
					break;
				}
				case settingTabEnum::tab9:
				{
					Widgets::PageHeader(IA(I18nKey.SettingsUI.DebugSoftware.N).c_str());
					Widgets::PageContentStart();
					if (Widgets::BeginSettingsCard("##debug-touch-test",
						"启用触摸测试模式",
						"开启后，使用输入设备在主画布上产生输入，即刻开始测试。", "\ue7c9"))
					{
						if (ImFluent::AccentButton("开启"))
							ChangeStateModeToTouchTest();
						Widgets::EndSettingsCard();
					}

					Widgets::SectionHeader("实时状态");
					const bool debugCardVisible = ImFluent::BeginCard("##debug-output",
						{ ImGui::GetContentRegionAvail().x, 520.0F * settingGlobalScale },
						ImFluentCardStyle_Outlined);
					if (debugCardVisible)
					{
						wstring text;
						text += L"输入设备按下：";
						text += rtsDown ? L"是" : L"否";
						text += L"\n输入设备点：" + to_wstring(rtsNum);
						text += L"\n触摸设备点：" + to_wstring(touchNum) + L"\n";

						for (int i = 0; i < rtsNum; ++i)
						{
							std::shared_lock<std::shared_mutex> positionLock(touchPosSm);
							const TouchMode mode = TouchPos[TouchList[i]];
							positionLock.unlock();
							std::shared_lock<std::shared_mutex> speedLock(touchSpeedSm);
							const double speed = TouchSpeed[TouchList[i]];
							speedLock.unlock();

							text += L"pid" + to_wstring(TouchList[i]) + L" | ";
							if (mode.type == 0) text += L"触摸点";
							else if (mode.type == 1)
								text += mode.isInvertedCursor ? L"触控笔(倒置)" : L"触控笔";
							else if (mode.type == 2) text += L"鼠标(左键)";
							else if (mode.type == 3) text += L"鼠标(右键)";
							text += L" | 坐标 " + to_wstring(mode.pt.x) + L","
								+ to_wstring(mode.pt.y);
							text += L" | 速度 " + to_wstring(speed);
							if (mode.type == 0)
								text += L" | 面积 " + to_wstring(mode.touchWidth) + L","
									+ to_wstring(mode.touchHeight);
							else
								text += L" | 面积(此设备不支持)";
							if (mode.type == 1)
								text += mode.isInvertedCursor
									? L" | 压力(落笔时) " + to_wstring(mode.pressure)
									: L" | 压力 " + to_wstring(mode.pressure);
							else
								text += L" | 压力(此设备不支持)";
							text += L"\n";
						}

						text += L"\nTouchList ";
						for (const auto& value : TouchList)
							text += to_wstring(value) + L" ";
						text += L"\nTouchTemp ";
						for (const auto& value : TouchTemp)
							text += to_wstring(value.pid) + L" ";

						text += L"\n\n撤回库当前大小：" + to_wstring(RecallImage.size())
							+ L"(峰值" + to_wstring(RecallImagePeak) + L")";
						text += L"\n首次绘制状态：";
						text += FirstDraw ? L"是" : L"否";
						const wstring pptLinkState = pptComVersion.substr(0, 7) == L"Error: "
							? L"发生错误 " + pptComVersion
							: L"连接成功，版本 " + pptComVersion;
						text += L"\n\nPPT COM接口 联动组件 状态：" + pptLinkState;
						text += L"\nPPT 状态：";
						text += PptInfoState.TotalPage != -1 ? L"正在播放" : L"未播放";
						text += L"\nPPT 总页面数：" + to_wstring(PptInfoState.TotalPage);
						text += L"\nPPT 当前页序号：" + to_wstring(PptInfoState.CurrentPage);
						text += L"\n\n监视器数量：" + to_wstring(DisplaysNumber);
						text += L"\n主监视器像素宽度：" + to_wstring(MainMonitor.MonitorWidth) + L"px";
						text += L"\n主监视器像素高度：" + to_wstring(MainMonitor.MonitorHeight) + L"px";
						text += L"\n主监视器物理宽度：" + to_wstring(MainMonitor.MonitorPhyWidth) + L"cm";
						text += L"\n主监视器物理高度：" + to_wstring(MainMonitor.MonitorPhyHeight) + L"cm";

						if (ImFluent::BeginScrollView("##debug-output-scroll",
							{ 0.0F, 480.0F * settingGlobalScale }))
						{
							const string utf8Text = utf16ToUtf8(text);
							ImGui::TextWrapped("%s", utf8Text.c_str());
							ImFluent::EndScrollView();
						}
					}
					ImFluent::EndCard();
					break;
				}
				}
				ImGui::EndChild();
				// 页面 transition 的 alpha 只在页面内容 scope 内生效。
				ImGui::PopStyleVar();
				ImFluent::NavigationViewEndContent();

				if (overlayNavigation)
				{
					ImFluent::EndSplitViewContent();
					if (ImFluent::BeginSplitViewPane())
					{
						ImFluentNavViewMode mode = narrowPaneOpen
							? ImFluentNavViewMode_LeftOpen : ImFluentNavViewMode_LeftCompact;
						ImFluent::BeginNavigationView("##setting-overlay-nav", &mode);
						renderNavigationItems();
						ImFluent::EndNavigationView();
						narrowPaneOpen = mode == ImFluentNavViewMode_LeftOpen
							&& !navigationActivated;
						ImFluent::EndSplitViewPane();
					}
					ImFluent::EndSplitView();
				}

				ImGui::End();
			}

			// 渲染
			ImFluent::PopFluentStyle();
			ImGui::EndFrame();
			ImGui::Render();
			const bool transparentBackdrop = settingBackdropMode.load(
				memory_order_acquire) != Inkeys::UI::Setting::BackdropMode::Solid;
			const ImVec4 clearColorValue = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			const float clearColor[4] = {
				transparentBackdrop ? 0.0F : clearColorValue.x * clearColorValue.w,
				transparentBackdrop ? 0.0F : clearColorValue.y * clearColorValue.w,
				transparentBackdrop ? 0.0F : clearColorValue.z * clearColorValue.w,
				transparentBackdrop ? 0.0F : clearColorValue.w
			};
			g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
			g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			const HRESULT result = g_pSwapChain->Present(1, 0);
			g_SwapChainOccluded = (result == DXGI_STATUS_OCCLUDED);
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(result))
				settingFrameResult = FrameResult::DeviceLost;
			else if (FAILED(result))
				settingFrameResult = FrameResult::Retry;
			else
				settingFrameResult = FrameResult::Continue;
			co_await suspend_always{};
		}

		//::ShowWindow(setting_window, SW_HIDE);

		// HWND 会晚于 Setting session 销毁，先撤销可选材质避免残留透明状态。
		DisableSettingBackdrop(setting_window);
		settingBackdropMode.store(
			Inkeys::UI::Setting::BackdropMode::Solid, memory_order_release);
		// Shutdown 在渲染线程按 backend -> SRV -> presentation 逆序释放。
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		CleanupSettingTextures();
		CleanupSettingTextureCache();
		io.Fonts->Clear();
		ImFluent::ResetContext();
		ImGui::DestroyContext();
		CleanupDeviceD3D();
	settingSessionEpoch = 0;
	#undef CloseProgram
	#undef RestartProgram
	#undef ShellExecuteW
	#undef SetStartupState
	#undef PptComWriteSetting
	#undef WriteSetting
	co_return;
}

namespace
{
	void DrainSettingSessionOnRenderThread() noexcept
	{
		if (!settingSession.Done())
		{
			settingSessionShouldStop.store(true, memory_order_release);
			lock_guard imguiLock(settingImguiMutex);
			settingSession.Resume();
		}
		settingSession.Reset();
		settingSessionEpoch = 0;
		{
			lock_guard lock(settingDrainMutex);
			settingSessionDrained = true;
		}
		settingDrainCondition.notify_all();
	}

	FrameResult RenderSettingFrame(const FrameContext& context)
	{
		Inkeys::UI::Setting::SessionDecision decision;
		Inkeys::UI::Setting::ResizeSnapshot resize;
		uint64_t fontRebuildSerial = 0;
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetOccluded(g_SwapChainOccluded);
			decision = settingSessionState.Resolve(
				context.epoch.generation, !settingSession.Done(),
				g_pSwapChain != nullptr);
			resize = settingSessionState.Resize();
			fontRebuildSerial = settingSessionState.FontRebuildSerial();
			if (decision.consumeBusinessCompletion)
			{
				settingLastBusinessCompletion = settingSessionState.BusinessCompletion();
				settingSessionState.ConsumeBusinessCompletion(
					settingLastBusinessCompletion.serial);
			}
		}

		if (decision.releasePresentation)
		{
			CleanupPresentation();
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ReleasePresentation();
		}

		if (decision.initializeResident)
		{
			settingSessionShouldStop.store(false, memory_order_release);
			settingFrameContext = context;
			settingFrameResult = FrameResult::Retry;
			settingSession = RunSettingSession();
			{
				lock_guard lock(settingDrainMutex);
				settingSessionDrained = false;
			}
			{
				lock_guard imguiLock(settingImguiMutex);
				settingSession.Resume();
			}

			const bool succeeded = !settingSession.Done();
			if (succeeded)
			{
				lock_guard stateLock(settingStateMutex);
				settingSessionState.CommitEpoch(context.epoch.generation);
			}
			else
			{
				settingSession.Reset();
				settingSessionEpoch = 0;
				lock_guard lock(settingDrainMutex);
				settingSessionDrained = true;
				settingDrainCondition.notify_all();
			}
			{
				lock_guard initializeLock(settingInitializeMutex);
				settingInitializeSucceeded = succeeded;
				settingInitializeCompleted = true;
			}
			settingInitializeCondition.notify_all();
			if (!succeeded) return FrameResult::Retry;
		}

		if (decision.rebuildDeviceResources && !settingSession.Done())
		{
			lock_guard imguiLock(settingImguiMutex);
			CleanupPresentation();
			if (ImGui::GetIO().BackendRendererUserData)
				ImGui_ImplDX11_Shutdown();
			CleanupSettingTextures();
			CleanupDeviceD3D();
			const bool acquired = AcquireDeviceLease(context.epoch);
			const bool backendInitialized = acquired
				&& ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
			if (!backendInitialized || !ImGui_ImplDX11_CreateDeviceObjects()
				|| !RecreateSettingTextures())
			{
				// epoch 失败后保持 resident context，但撤销半成品 device 资源供下次重试。
				if (backendInitialized) ImGui_ImplDX11_Shutdown();
				CleanupSettingTextures();
				CleanupDeviceD3D();
				return FrameResult::Retry;
			}
			settingSessionEpoch = context.epoch.generation;
			lock_guard stateLock(settingStateMutex);
			settingSessionState.CommitEpoch(context.epoch.generation);
		}

		if (decision.rebuildFonts && !settingSession.Done())
		{
			lock_guard imguiLock(settingImguiMutex);
			ImGui_ImplDX11_InvalidateDeviceObjects();
			if (!RebuildSettingFonts()
				|| !ImGui_ImplDX11_CreateDeviceObjects())
				return FrameResult::Retry;
			Widgets::style.ApplyGlobal(12.0F);
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ConsumeFontRebuild(fontRebuildSerial);
		}

		const uint64_t themeSerial = settingThemeSerial.load(memory_order_acquire);
		if (!settingSession.Done() && themeSerial != settingConsumedThemeSerial)
		{
			// 主题消息在隐藏态也立即更新 resident style，不等待下一次 Show。
			lock_guard imguiLock(settingImguiMutex);
			ApplySystemTheme(setting_window);
			settingConsumedThemeSerial = themeSerial;
		}

		// 只有 Move 可复用 compositor 缓存；Size 必须继续 resize/render/present。
		if (settingInteractiveOperation.load(memory_order_acquire)
			== Inkeys::UI::Setting::InteractiveWindowOperation::Move)
			return FrameResult::Idle;

		if (decision.createPresentation && !g_pSwapChain)
		{
			if (!CreatePresentation(setting_window, context.epoch))
				return FrameResult::Retry;
		}

		if (!decision.render || settingSession.Done() || !g_pSwapChain)
			return FrameResult::Idle;

		if (decision.probeOcclusion && g_pSwapChain)
		{
			const HRESULT probeResult = g_pSwapChain->Present(0, DXGI_PRESENT_TEST);
			if (probeResult == DXGI_STATUS_OCCLUDED) return FrameResult::Retry;
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(probeResult))
				return FrameResult::DeviceLost;
			if (FAILED(probeResult)) return FrameResult::Retry;
			g_SwapChainOccluded = false;
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetOccluded(false);
		}

		if (decision.resize && !settingSession.Done() && g_pSwapChain)
		{
			const HRESULT resizeResult = ResizeSwapChain(resize.width, resize.height);
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(resizeResult))
				return FrameResult::DeviceLost;
			if (FAILED(resizeResult))
			{
				CleanupPresentation();
				lock_guard stateLock(settingStateMutex);
				settingSessionState.ReleasePresentation();
				return FrameResult::Retry;
			}
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ConsumeResize(resize.serial);
		}

		settingFrameContext = context;

		{
			lock_guard imguiLock(settingImguiMutex);
			settingSession.Resume();
		}
		if (settingSession.Done())
		{
			settingSession.Reset();
			settingSessionEpoch = 0;
			lock_guard lock(settingDrainMutex);
			settingSessionDrained = true;
			settingDrainCondition.notify_all();
		}
		else
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.CommitEpoch(context.epoch.generation);
		}
		return settingFrameResult;
	}
}

namespace Inkeys::UI::Setting
{
	bool Initialize()
	{
		lock_guard lock(settingLifecycleMutex);
		if (settingInitialized.load(memory_order_acquire)) return true;
		if (!setting_window || !settingBusinessQueue.Start()) return false;
		RECT client{};
		if (GetClientRect(setting_window, &client))
		{
			SettingWindowWidth = static_cast<int>(client.right - client.left);
			SettingWindowHeight = static_cast<int>(client.bottom - client.top);
			lock_guard stateLock(settingStateMutex);
			settingSessionState.QueueResize(
				static_cast<UINT>(SettingWindowWidth),
				static_cast<UINT>(SettingWindowHeight));
		}
		if (!Inkeys::UI::RenderPipeline::Register(
			Inkeys::UI::RenderPipeline::Client::Settings, RenderSettingFrame))
		{
			settingBusinessQueue.Stop();
			return false;
		}
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(false);
		}
		{
			lock_guard initializeLock(settingInitializeMutex);
			settingInitializeCompleted = false;
			settingInitializeSucceeded = false;
		}
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		{
			unique_lock initializeLock(settingInitializeMutex);
			settingInitializeCondition.wait(initializeLock,
				[] { return settingInitializeCompleted; });
			if (!settingInitializeSucceeded)
			{
				Inkeys::UI::RenderPipeline::Unregister(
					Inkeys::UI::RenderPipeline::Client::Settings);
				settingBusinessQueue.Stop();
				return false;
			}
		}
		settingInitialized.store(true, memory_order_release);
		return true;
	}

	void Shutdown() noexcept
	{
		unique_lock lifecycleLock(settingLifecycleMutex);
		if (!settingInitialized.exchange(false, memory_order_acq_rel)) return;
		settingInteractiveOperation.store(
			InteractiveWindowOperation::None, memory_order_release);
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(false);
		}
		settingBusinessQueue.Enqueue({ SettingBusinessKind::HideWindow });
		const bool drainPosted = Inkeys::UI::RenderPipeline::PostControl([]
			{
				DrainSettingSessionOnRenderThread();
				lock_guard stateLock(settingStateMutex);
				settingSessionState.ReleaseResident();
			});
		if (drainPosted)
		{
			unique_lock drainLock(settingDrainMutex);
			settingDrainCondition.wait(drainLock, [] { return settingSessionDrained; });
		}
		else if (IDTLogger)
		{
			IDTLogger->error(
				"[Setting] 渲染管线已停止，无法在线程内排空设置会话");
		}
		Inkeys::UI::RenderPipeline::Unregister(
			Inkeys::UI::RenderPipeline::Client::Settings);
		settingBusinessQueue.Stop();
	}

	void Show()
	{
		if (!settingInitialized.load(memory_order_acquire)) return;
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(true);
		}
		settingBusinessQueue.Enqueue({ SettingBusinessKind::ShowWindow });
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
	}

	void Hide()
	{
		if (!settingInitialized.load(memory_order_acquire)) return;
		// Hide 可能由 modal loop 外部触发，不能依赖随后一定收到 EXIT。
		settingInteractiveOperation.store(
			InteractiveWindowOperation::None, memory_order_release);
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(false);
		}
		settingBusinessQueue.Enqueue({ SettingBusinessKind::HideWindow });
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
	}

	void Toggle()
	{
		if (IsVisible()) Hide();
		else Show();
	}

	bool IsVisible() noexcept
	{
		lock_guard stateLock(settingStateMutex);
		return settingSessionState.IsVisible();
	}

	WNDPROC WindowProc() noexcept
	{
		return ImGuiWndProc;
	}
}

WNDPROC SettingWindowProc() noexcept
{
	return Inkeys::UI::Setting::WindowProc();
}
