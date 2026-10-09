module;

#include "Setting.Wrap.h"

// ImFluent 使用 imgui_internal.h，升级 Dear ImGui 时必须同步验证固定快照。
#if IMGUI_VERSION_NUM != 19270
#error "Inkeys Setting requires Dear ImGui 1.92.7 (IMGUI_VERSION_NUM == 19270)"
#endif

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../IdtImage.h"
#include "../../../IdtMagnification.h"
#include "../../../IdtOther.h"
#include "../../../IdtPlug-in.h"
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Bridge.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../Window/Window.Legacy.hpp"
#include "Setting.SessionState.h"
#include "Setting.Pages.h"
#include "../../../SuperTop/IdtSuperTop.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.UI.Setting;

import Inkeys.Display;
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
import Inkeys.UI.MessageBox;

// 从 imgui_impl_win32.cpp 中前向声明消息处理器
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

using namespace std;
namespace Design = Inkeys::UI::Setting::Design;

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
	atomic<float> settingMinimumUserScale = 1.0F;
	bool settingMinimumClientPending = false; // 仅 HWND 线程访问。
	uint64_t settingConsumedThemeSerial = 0;
	FrameContext settingFrameContext;
	FrameResult settingFrameResult = FrameResult::Idle;
	bool settingNavigationClockIdle = true; // 仅共享渲染线程访问，跳过恢复帧的隐藏/移动耗时。
	uint64_t settingSessionEpoch = 0;
	Inkeys::UI::Setting::SessionState settingSessionState;
	Inkeys::UI::Setting::BusinessCompletionSnapshot settingLastBusinessCompletion;
	Inkeys::UI::Setting::TitleBarGeometry settingTitleBarGeometry;
	float settingTitleTextWidth = 0.0F;
	float settingVersionTextWidth = 0.0F;

	constexpr UINT SettingFrameThemeMessage = WM_APP + 0x531;
	constexpr UINT SettingEnsureMinimumMessage = WM_APP + 0x532;

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

	void SetSettingAcrylic(HWND hwnd, bool enabled, bool darkMode = false) noexcept
	{
		const auto setCompositionAttribute =
			QuerySetWindowCompositionAttribute();
		if (!hwnd || !setCompositionAttribute) return;

		SettingAccentPolicy policy{};
		policy.state = enabled
			? SettingAccentState::AcrylicBlurBehind
			: SettingAccentState::Disabled;
		// GradientColor 使用 AABBGGRR；窗口线程消费已解析的主题快照。
		policy.gradientColor = enabled ? (darkMode ? 0xCC202020U : 0xCCF7F7F7U) : 0U;
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
		ApplySettingBackdrop(HWND hwnd, bool darkMode) noexcept
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
			policy.gradientColor = darkMode ? 0xCC202020U : 0xCCF7F7F7U;
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

	[[nodiscard]] Inkeys::UI::Setting::WindowFrameRect FrameRect(const RECT& rect) noexcept
	{
		return { static_cast<int>(rect.left), static_cast<int>(rect.top),
			static_cast<int>(rect.right), static_cast<int>(rect.bottom) };
	}

	[[nodiscard]] Inkeys::UI::Setting::WindowFrameInsets QuerySettingFrameInsets(
		HWND hwnd, bool maximized = false) noexcept
	{
		const RECT frame = Inkeys::Window::QuerySettingClientFrameInsets(
			hwnd, maximized, QuerySettingDpi(hwnd));
		return { static_cast<int>(frame.left), static_cast<int>(frame.top),
			static_cast<int>(frame.right), static_cast<int>(frame.bottom) };
	}

	void EnsureSettingMinimumClient(HWND hwnd) noexcept
	{
		if (IsZoomed(hwnd) || IsIconic(hwnd)) return;
		RECT outer{}, client{};
		if (!GetWindowRect(hwnd, &outer) || !GetClientRect(hwnd, &client)) return;
		// 不携带旧倍率；窗口线程读取最新值，并只扩大不足的客户区。
		const float scale = Inkeys::UI::Setting::EffectiveScale(
			QuerySettingDpi(hwnd), settingMinimumUserScale.load(memory_order_acquire));
		const int clientWidth = static_cast<int>(client.right - client.left);
		const int clientHeight = static_cast<int>(client.bottom - client.top);
		const int width = (std::max)(clientWidth, Inkeys::UI::Setting::ScaleDip(
			Inkeys::UI::Setting::MinimumWidthDip, scale));
		const int height = (std::max)(clientHeight, Inkeys::UI::Setting::ScaleDip(
			Inkeys::UI::Setting::MinimumHeightDip, scale));
		if (width == clientWidth && height == clientHeight) return;
		const auto measuredOuter = FrameRect(outer);
		const int left = measuredOuter.left + measuredOuter.Width() / 2 - width / 2;
		const int top = measuredOuter.top + measuredOuter.Height() / 2 - height / 2;
		MONITORINFO monitor{ sizeof(monitor) };
		if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
			monitor.rcWork = {};
		// 复用实际 outer/client 差值，避免重新计入已移除的原生 caption。
		const auto corrected = Inkeys::UI::Setting::ResolveWindowFrameCreationCorrection(
			{ left, top, left + width, top + height }, measuredOuter,
			{ clientWidth, clientHeight }, FrameRect(monitor.rcWork));
		if (corrected.Width() != measuredOuter.Width() || corrected.Height() != measuredOuter.Height())
			SetWindowPos(hwnd, nullptr, corrected.left, corrected.top,
				corrected.Width(), corrected.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void UpdateSettingScale(UINT dpi) noexcept
	{
		settingUserScale = Inkeys::UI::Setting::NormalizeUserScale(
			setlist.settingGlobalScale);
		settingMinimumUserScale.store(settingUserScale, memory_order_release);
		settingSystemDpiScale = Inkeys::UI::Setting::DpiScale(dpi);
		settingGlobalScale = settingSystemDpiScale * settingUserScale;
	}

	void ApplySettingTheme(HWND hwnd) noexcept
	{
		// ImGui/ImFluent 只在渲染线程帧边界换色，系统主题消息不覆盖用户偏好。
		Design::ApplyPalette(setlist.settingDarkMode);
		ImFluent::GetStyle().NavItemHeight = 36.0F;
		Widgets::style.ApplyGlobal(Design::ScrollbarTrackSize);
		Design::ApplyScrollbars();
		if (settingBackdropMode.load(memory_order_acquire)
			!= Inkeys::UI::Setting::BackdropMode::Solid)
		{
			ImFluent::GetStyle().Colors[ImFluentCol_SolidBgBase].w = 0.0F;
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.0F;
		}
		if (hwnd)
		{
			const ImU32 base = Design::AppBase;
			const COLORREF captionColor = RGB((base >> IM_COL32_R_SHIFT) & 0xFF,
				(base >> IM_COL32_G_SHIFT) & 0xFF, (base >> IM_COL32_B_SHIFT) & 0xFF);
			// 不跨线程同步改 HWND 样式；独立消息只携带已冻结的明暗和颜色。
			(void)PostMessageW(hwnd, SettingFrameThemeMessage,
				setlist.settingDarkMode, static_cast<LPARAM>(captionColor));
		}
	}

	void ApplySettingFrameTheme(HWND hwnd, bool darkMode, COLORREF captionColor) noexcept
	{
		const BOOL darkFrame = darkMode;
		(void)DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
			&darkFrame, sizeof(darkFrame));
		(void)DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR,
			&captionColor, sizeof(captionColor));
		const COLORREF textColor = darkMode ? RGB(243, 243, 243) : RGB(32, 32, 32);
		(void)DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
		const COLORREF borderColor = darkMode ? RGB(66, 66, 66) : RGB(213, 213, 213);
		(void)DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
		const auto backdrop = ApplySettingBackdrop(hwnd, darkMode);
		if (settingBackdropMode.exchange(backdrop, memory_order_acq_rel) != backdrop)
		{
			// 可选材质成功/回退后，由渲染线程统一更新根背景的 alpha。
			settingThemeSerial.fetch_add(1, memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Settings);
		}
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
		if (geometry.themeToggle.Contains(x, y) || geometry.debug.Contains(x, y)
			|| (geometry.versionVisible && geometry.version.Contains(x, y)))
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
			{
				const auto title = I18n::getWOr(
					I18nKey.Dialogs.Common.TipsTitle, L"Inkeys Tips");
				const auto okLabel = I18n::getWOr(
					I18nKey.Dialogs.Common.OK, L"OK");
				auto request = Inkeys::UI::MessageBox::MakeOkRequest(
					title.c_str(), command.text.c_str());
				request.language = I18n::languageId();
				request.labels.ok = okLabel.c_str();
				request.owner = Inkeys::Window::GetService().Handle(
					Inkeys::Window::WindowRole::Setting);
				request.requireOwner = true;
				request.fallback.modality =
					Inkeys::UI::MessageBox::SystemModality::System;
				return Inkeys::UI::MessageBox::Show(request)
					!= Inkeys::UI::MessageBox::Result::Failed;
			}
			case SettingBusinessKind::ConfirmRestart:
			{
				const auto title = I18n::getWOr(
					I18nKey.Dialogs.Common.TipsTitle, L"Inkeys Tips");
				const auto okLabel = I18n::getWOr(
					I18nKey.Dialogs.Common.OK, L"OK");
				const auto cancelLabel = I18n::getWOr(
					I18nKey.Dialogs.Common.Cancel, L"Cancel");
				auto request = Inkeys::UI::MessageBox::MakeOkCancelRequest(
					title.c_str(), command.text.c_str());
				request.language = I18n::languageId();
				request.labels.ok = okLabel.c_str();
				request.labels.cancel = cancelLabel.c_str();
				request.owner = Inkeys::Window::GetService().Handle(
					Inkeys::Window::WindowRole::Setting);
				request.requireOwner = true;
				request.fallback.modality =
					Inkeys::UI::MessageBox::SystemModality::System;
				if (Inkeys::UI::MessageBox::Show(request)
					== Inkeys::UI::MessageBox::Result::Ok)
					RestartProgram();
				return true;
			}
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
				if (executable != pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe" ||
					command.directory != pluginPath + L"DesktopDrawpadBlocker" ||
					command.digest != ddbInteractionSetList.DdbSHA256) return false;
				const bool configWritten = WriteSettingJson(command.jsonPayload);
				if (command.flag)
				{
					if (!configWritten) return false;
					error_code ec;
					filesystem::create_directories(command.directory, ec);
					if (ec) return false;
					const DWORD directoryAttributes = GetFileAttributesW(command.directory.c_str());
					const DWORD executableAttributes = GetFileAttributesW(executable.c_str());
					if (directoryAttributes == INVALID_FILE_ATTRIBUTES ||
						!(directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
						(directoryAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
						(executableAttributes != INVALID_FILE_ATTRIBUTES &&
							(executableAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))))
						return false;
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
					if (extract && !Inkeys::Load::ExtractResourceFile(
						executable, L"EXE", MAKEINTRESOURCE(237))) return false;
					if (!isProcessRunning(executable.c_str()))
					{
						WriteDdbInteractionJson(command.ddbOpenJsonPayload);
						return LaunchVerifiedDesktopDrawpadBlocker(command.secondaryFlag);
					}
					return true;
				}
				// 禁用请求即使配置写回失败也要撤销正在运行的插件与自启。
				const bool closeRequested = WriteDdbInteractionJson(command.ddbCloseJsonPayload);
				SetStartupState(false, executable, L"$Inkeys_DesktopDrawpadBlocker");
				error_code ec;
				filesystem::remove(command.directory + L"\\start_up.signal", ec);
				return configWritten && closeRequested;
			}
			case SettingBusinessKind::RestartDdb:
			{
				if (command.text != pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe")
					return false;
				const bool configWritten = WriteSettingJson(command.jsonPayload);
				// 提升必须先持久化；降权即使写盘失败仍优先降低本次权限。
				if (!configWritten && command.flag) return false;
				if (!isProcessRunning(command.text.c_str())) return configWritten;
				WriteDdbInteractionJson(command.ddbCloseJsonPayload);
				for (int i = 0; i < 25 && isProcessRunning(command.text.c_str()); ++i)
					this_thread::sleep_for(chrono::milliseconds(500));
				if (isProcessRunning(command.text.c_str())) return false;
				WriteDdbInteractionJson(command.ddbOpenJsonPayload);
				return LaunchVerifiedDesktopDrawpadBlocker(command.flag) && configWritten;
			}
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
			command.jsonPayload = CaptureSettingJson();
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
		Inkeys::UI::Ppt::NotifyConfigurationChanged(Inkeys::UI::Ppt::ConfigGroup::All);
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
		// 用户已确认重启；不能让旧业务队列阻止 15 秒监督器启动。
		::RestartProgram();
	}

	void QueueClose()
	{
		// 用户已确认关闭；原队列仍由正常退出流程排空。
		::CloseProgram();
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
	// 创建、客户区、最大化及命中使用同一 frame 合同，保留系统 move/size loop。
	switch (msg)
	{
	case SettingEnsureMinimumMessage:
		if (!settingSessionShouldStop.load(memory_order_acquire))
		{
			settingMinimumClientPending = IsZoomed(hWnd) || IsIconic(hWnd);
			if (!settingMinimumClientPending) EnsureSettingMinimumClient(hWnd);
		}
		return 0;
	case SettingFrameThemeMessage:
		if (!settingSessionShouldStop.load(memory_order_acquire))
			ApplySettingFrameTheme(hWnd, wParam != FALSE, static_cast<COLORREF>(lParam));
		return 0;
	case WM_NCCALCSIZE:
	{
		if (!lParam) return 0;
		RECT& proposed = wParam
			? reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]
			: *reinterpret_cast<RECT*>(lParam);
		// 恢复态顶部仅保留 DWM 描边；最大化仍完整内缩系统 frame，与 USER32 工作区一致。
		const auto client = Inkeys::UI::Setting::InsetWindowFrameRect(
			FrameRect(proposed), QuerySettingFrameInsets(hWnd, IsZoomed(hWnd) != FALSE));
		proposed = { client.left, client.top, client.right, client.bottom };
		return 0;
	}
	case WM_NCPAINT:
		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	case WM_NCHITTEST:
	{
		RECT outer{}, client{};
		POINT clientOrigin{};
		if (!GetWindowRect(hWnd, &outer) || !GetClientRect(hWnd, &client)
			|| !ClientToScreen(hWnd, &clientOrigin))
			return ::DefWindowProcW(hWnd, msg, wParam, lParam);
		OffsetRect(&client, clientOrigin.x, clientOrigin.y);
		const LRESULT clientHit = HitTestSettingClientTitleBar(hWnd, lParam);
		const SIZE sizing = Inkeys::Window::QuerySettingFrameThickness(QuerySettingDpi(hWnd));
		using Inkeys::UI::Setting::WindowFrameHit;
		switch (Inkeys::UI::Setting::HitTestWindowFrame(FrameRect(outer), FrameRect(client),
			GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), IsZoomed(hWnd) != FALSE,
			static_cast<int>(sizing.cy), clientHit != HTCAPTION))
		{
		case WindowFrameHit::TopLeft: return HTTOPLEFT;
		case WindowFrameHit::TopRight: return HTTOPRIGHT;
		case WindowFrameHit::BottomLeft: return HTBOTTOMLEFT;
		case WindowFrameHit::BottomRight: return HTBOTTOMRIGHT;
		case WindowFrameHit::Left: return HTLEFT;
		case WindowFrameHit::Right: return HTRIGHT;
		case WindowFrameHit::Top: return HTTOP;
		case WindowFrameHit::Bottom: return HTBOTTOM;
		case WindowFrameHit::Outside: return HTNOWHERE;
		default: return clientHit;
		}
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
		auto* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
		if (!minMax) return 0;
		MONITORINFO monitor{ sizeof(monitor) };
		const bool hasMonitor = GetMonitorInfoW(
			MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;
		const auto frame = QuerySettingFrameInsets(hWnd);
		const float scale = Inkeys::UI::Setting::EffectiveScale(
			QuerySettingDpi(hWnd), settingUserScale);
		const auto minimum = Inkeys::UI::Setting::ResolveMinimumWindowFrame(
			Inkeys::UI::Setting::ScaleDip(Inkeys::UI::Setting::MinimumWidthDip, scale),
			Inkeys::UI::Setting::ScaleDip(Inkeys::UI::Setting::MinimumHeightDip, scale), frame,
			hasMonitor ? monitor.rcWork.right - monitor.rcWork.left : 0,
			hasMonitor ? monitor.rcWork.bottom - monitor.rcWork.top : 0);
		// 保留 USER32 的 primary-monitor 最大化协议，避免目标尺寸被再次补偿。
		Inkeys::UI::Setting::ApplyWindowFrameMinimumTrack(*minMax, minimum);
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
		// composition 开关会改变恢复态顶部 inset，立即重算而不等待用户 resize。
		SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
			SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		[[fallthrough]];
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
		// 最大化/最小化期间的倍率请求留到恢复，再排队一次；普通 resize 不重复扩窗。
		if (wParam == SIZE_RESTORED && settingMinimumClientPending
			&& !IsZoomed(hWnd) && PostMessageW(hWnd, SettingEnsureMinimumMessage, 0, 0))
			settingMinimumClientPending = false;
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
		const auto snapshot = Inkeys::Display::GetSnapshot();
		const auto* primary = snapshot ? snapshot->Primary() : nullptr;
		RECT workArea = primary ? primary->workArea : RECT{ 0, 0, 960, 700 };
		if (!primary) (void)SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
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
	ImFontDesignMain = nullptr;
	ImFontDesignStrong = nullptr;
	ImFontDesignIcons = nullptr;
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

	// 校准字体仅绑定新 Shell/Home/General，避免改变旧页 TextWrapped 的行距。
	auto addDesignFace = [&](UINT resource, UINT fallback, ImFont** result)
		{
			auto config = Inkeys::UI::Setting::Design::HarmonyFontConfig(settingGlobalScale);
			if (!AddSettingFontResource(resource,
				Inkeys::UI::Setting::Design::FontReferenceSize, &config, result)) return false;
			if (!traditionalChinese) return true;
			config.MergeMode = true;
			return AddSettingFontResource(fallback,
				Inkeys::UI::Setting::Design::FontReferenceSize, &config);
		};
	if (!addDesignFace(regularResource, 198U, &ImFontDesignMain)
		|| !addDesignFace(strongResource, 297U, &ImFontDesignStrong)) return false;
	auto designIconConfig = Inkeys::UI::Setting::Design::IconFontConfig(settingGlobalScale);
	if (!AddSettingFontResource(257U, Inkeys::UI::Setting::Design::FontReferenceSize,
		&designIconConfig, &ImFontDesignIcons)) return false;
	ImFluent::SetIconFont(ImFontDesignIcons);

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
	// 设置 I/O 等业务投递 FIFO worker；确认的退出意图直接建立 15 秒保护。
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

		const uint64_t initialThemeSerial = settingThemeSerial.load(memory_order_acquire);
		ApplySettingTheme(setting_window);
		settingConsumedThemeSerial = initialThemeSerial;

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
			int TeachingSafetyMode = std::clamp(
				setlist.regularSetting.teachingSafetyMode.load(), 0, 3);
		}RegularSetting;

		struct
		{
			bool Enable = setlist.saveSetting.enable;
			int SaveDays = setlist.saveSetting.saveDays;
		}SaveSetting;

		int PaintDevice = setlist.paintDevice;
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
		bool BottomSideBothWidgetScaleUnifie = true;
		bool MiddleSideBothWidgetScaleUnifie = true;

		bool PptComShowLoadingScreen = pptComSetlist.showLoadingScreen;
		bool MemoryWidgetPosition = pptComSetlist.memoryWidgetPosition;
		bool ShowBottomBoth = pptComSetlist.showBottomBoth;
		bool ShowMiddleBoth = pptComSetlist.showMiddleBoth;
		float BottomBothWidth = pptComSetlist.bottomBothWidth;
		float BottomBothHeight = pptComSetlist.bottomBothHeight;
		float MiddleBothWidth = pptComSetlist.middleBothWidth;
		float MiddleBothHeight = pptComSetlist.middleBothHeight;

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
				bool EnableDirectComposition = Inkeys::config.Experimental.Inkeys3.Draw3.EnableDirectComposition;
				Inkeys::UI::Setting::StartupPreviewPreference StartupPreviewPreferenceState{
					Inkeys::config.Experimental.Inkeys3.UI3.StartupPreview.Enable };
				bool TouchAreaConsoleOutput = Inkeys::config.Experimental.Inkeys3.ConsoleOutput.TouchArea;
				bool PptCOMConsoleOutput = Inkeys::config.Experimental.Inkeys3.ConsoleOutput.PptCOM;
				bool Draw3ConsoleOutput = Inkeys::config.Experimental.Inkeys3.ConsoleOutput.Draw3;
				bool CursorConsoleOutput = Inkeys::config.Experimental.Inkeys3.ConsoleOutput.Cursor;
			}Inkeys3;
		}Experimental;

		// ==========

		wstring receivedData;

		int settingTab = 0;
		int settingPlugInTab = 0;
		Inkeys::UI::Setting::Design::NavigationState navigationState;
		Design::NavigationDestination destination;
		auto commitSettingScale = [&]()
		{
			if (SettingGlobalScale != setlist.settingGlobalScale)
			{
				setlist.settingGlobalScale = Inkeys::UI::Setting::NormalizeUserScale(SettingGlobalScale);
				UpdateSettingScale(QuerySettingDpi(setting_window));
				// 渲染线程只投递请求，窗口尺寸仍由所属 HWND 线程更新。
				if (setting_window) (void)PostMessageW(setting_window, SettingEnsureMinimumMessage, 0, 0);
				{
					lock_guard stateLock(settingStateMutex);
					settingSessionState.QueueFontRebuild();
				}
				Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Settings);
				WriteSetting();
			}
		};
		auto commitGeneralSliders = [&]()
		{
			// 导航可能使拖动控件下一帧不再渲染，离页时仍完成已有的释放保存合同。
			if (BarZoomSavePending || Experimental.Inkeys3.AnimationSpeedSavePending)
			{
				QueueConfigWrite();
				BarZoomSavePending = false;
				Experimental.Inkeys3.AnimationSpeedSavePending = false;
			}
			commitSettingScale();
		};
		auto navigateTo = [&](Design::NavigationDestination requested)
		{
			requested = Design::NormalizeDestination(requested);
			if (requested != destination) commitGeneralSliders();
			destination = requested;
			settingTab = static_cast<int>(destination.page);
			Design::NotifyNavigationDestination(navigationState, destination);
		};
		auto synchronizeDestination = [&]()
		{
			// 保留业务 switch 的原编号；旧配置/快捷键路由统一映射到通用子页。
			auto requested = Design::DestinationForPage(settingTab);
			if (settingTab == static_cast<int>(Design::PageId::General))
				requested.section = destination.section;
			navigateTo(requested);
		};

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
					// 保留已删除页面的编号空洞，导航和旧内部路由不随发布清理偏移。
					tab4 = 8,
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

				const float widthDip = static_cast<float>(SettingWindowWidth) / settingGlobalScale;
				navigationState.Resize(widthDip);
				synchronizeDestination();
				const float navigationDelta = settingNavigationClockIdle ? 0.0F : ImGui::GetIO().DeltaTime;
				settingNavigationClockIdle = false;
				Design::AdvanceNavigationAnimations(navigationState, navigationDelta,
					Experimental.Inkeys3.AnimationEnable, Experimental.Inkeys3.AnimationSpeedRate);
				Design::ConfigureRowAnimations(navigationDelta,
					Experimental.Inkeys3.AnimationEnable, Experimental.Inkeys3.AnimationSpeedRate);
				const auto shellGeometry = Inkeys::UI::Setting::Design::ResolveShellGeometry(
					widthDip, static_cast<float>(SettingWindowHeight) / settingGlobalScale, navigationState);

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
						// 根背景可为 Mica 透明，但 caption 始终以当前主题底色覆盖系统强调色。
						drawList->AddRectFilled(origin,
							{ origin.x + ImGui::GetWindowSize().x, origin.y + titleBarGeometry.height },
							Design::AppBase);
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
						if (titleBarGeometry.identity.right > titleLeft)
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
								SettingChromeColor(ImFluentCol_TextPrimary,
									active ? 1.0F : 0.55F), versionLabel.c_str());
							ImFluent::PopFont();
						}

						const ImRect themeBounds = ToImRect(titleBarGeometry.themeToggle, origin);
						ImGui::SetCursorScreenPos(themeBounds.Min);
						if (ImGui::InvisibleButton("##setting-theme-toggle", themeBounds.GetSize(),
							ImGuiButtonFlags_EnableNav))
						{
							setlist.settingDarkMode = !Design::IsDarkMode();
							WriteSetting();
							// 下一帧先换色，再 PushFluentStyle；主题切换不重建 atlas/device。
							settingThemeSerial.fetch_add(1, memory_order_release);
							Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Settings);
						}
						if (ImGui::IsItemHovered())
						{
							const float inset = 4.0F * settingGlobalScale;
							drawList->AddRectFilled({ themeBounds.Min.x, themeBounds.Min.y + inset },
								{ themeBounds.Max.x, themeBounds.Max.y - inset },
								SettingChromeColor(ImGui::IsItemActive()
									? ImFluentCol_SubtleFillTertiary : ImFluentCol_SubtleFillSecondary), inset);
						}
						ImFluent::DrawIcon(Design::IsDarkMode() ? "\xee\x9c\x86" : "\xee\x9c\x88",
							themeBounds.Min, themeBounds.Max, Design::NavigationGlyphSize,
							SettingChromeColor(ImFluentCol_TextPrimary, active ? 1.0F : 0.55F));

						if (Design::RenderDebugButton(titleBarGeometry.debug, destination.page == Design::PageId::Debug,
							navigationState, active ? 1.0F : 0.55F)) navigateTo({ Design::PageId::Debug });

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

				// 内容与导航单独排布；窄窗覆盖展开不改变正文宽度及其滚动位置。
				synchronizeDestination();
				const float contentAlpha = Design::ContentAlpha(navigationState);
				const bool blockContentInput = navigationState.BlockContentInput();
				const auto& contentGeometry = shellGeometry.content;
				ImGui::SetCursorPos({ Widgets::Dip(contentGeometry.left), Widgets::Dip(contentGeometry.top) });
				ImGui::PushStyleColor(ImGuiCol_ChildBg, Inkeys::UI::Setting::Design::PageSurface);
				ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0F);
				ImGui::BeginDisabled(blockContentInput);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
					{ Widgets::Dip(shellGeometry.gutter), Widgets::Dip(28.0F) });
				ImGui::PushID(settingTab);
				// 插件详情及通用子页各自保存滚动/展开状态，标签翻译不参与窗口标识。
				ImGui::PushID(settingTab == settingTabEnum::tab4 ? settingPlugInTab
					: settingTab == settingTabEnum::tab2 ? static_cast<int>(destination.section) : 0);
				ImGui::BeginChild("##setting-content", { Widgets::Dip(contentGeometry.Width()), Widgets::Dip(contentGeometry.Height()) },
					ImGuiChildFlags_AlwaysUseWindowPadding,
					blockContentInput ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None);
				ImGuiWindow* contentWindow = ImGui::GetCurrentWindow();
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
				// 所有设置页面共享校准字体；离开内容区后再恢复标题栏字体。
				Design::BindTextFonts(ImFontDesignMain, ImFontDesignStrong);
				ImFluent::PushFont(ImFluentTextStyle_Body);
				const float availablePageWidth = ImGui::GetContentRegionAvail().x;
				const float pageWidth = (std::min)(availablePageWidth,
					Widgets::Dip(Inkeys::UI::Setting::Design::PageMaximumWidth));
				const float pageOffset = max(0.0F, (availablePageWidth - pageWidth) * 0.5F);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pageOffset);
				(void)ImGui::BeginChild("##setting-page-content", { pageWidth, 0.0F },
					ImGuiChildFlags_AutoResizeY);

				// 页面只提交内容与业务事件，尺寸及文本对齐由 Design 层负责。
				const int contentPage = destination.page == Design::PageId::General
					&& destination.section == Design::GeneralSection::Storage
					? settingTabEnum::tabConfiguration : settingTab;
				switch (contentPage)
				{
					// 主页
				case settingTabEnum::tab1:
				{
					using Inkeys::UI::Setting::Design::HomeAction;
					const string version = utf16ToUtf8(editionVersion);
					const auto action = Inkeys::UI::Setting::Design::RenderHome({
						(ImTextureID)(intptr_t)TextureSettingSign[4],
						static_cast<float>(max(1, settingSign[4].height)) / max(1, settingSign[4].width), version.c_str(),
						(ImTextureID)(intptr_t)TextureSettingSign[3], (ImTextureID)(intptr_t)TextureSettingSign[7],
						static_cast<float>(max(1, settingSign[7].height)) / max(1, settingSign[7].width) });
					switch (action)
					{
					case HomeAction::Draw: settingTab = settingTabEnum::tab3; break;
					case HomeAction::Plugins: settingTab = settingTabEnum::tab4; settingPlugInTab = settingPlugInTabEnum::tabPlug1; break;
					case HomeAction::General: navigateTo({ Design::PageId::General, Design::GeneralSection::Basic }); break;
					case HomeAction::Language: settingTab = settingTabEnum::Language; break;
					case HomeAction::Support: settingTab = settingTabEnum::tab8; break;
					case HomeAction::Version: settingTab = settingTabEnum::tab6; break;
					case HomeAction::Website:
						ShellExecuteW(0, 0, I18n::isIdentifying(L"zh-CN") ? L"https://www.inkeys.top" : L"https://en.inkeys.top", 0, 0, SW_SHOW); break;
					case HomeAction::GitHub:
						ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/Inkeys", 0, 0, SW_SHOW); break;
					case HomeAction::Community:
						ShellExecuteW(0, 0, I18n::isIdentifying(L"zh-CN") ? L"https://qm.qq.com/cgi-bin/qm/qr?k=9V2l83dc0yP4UYeDF-NkTX0o7_TcYqlh&jump_from=webapi&authKey=LsLLUhb1KSzHYbc8k5nCQDqTtRcRUCEE3j+DdR9IgHaF/7JF7LLpY191hsiYEBz6" : L"https://github.com/Alan-CRL/Inkeys/discussions", 0, 0, SW_SHOW); break;
					case HomeAction::Bilibili:
						ShellExecuteW(0, 0, L"https://space.bilibili.com/1330313497", 0, 0, SW_SHOW); break;
					case HomeAction::Feedback:
						ShellExecuteW(0, 0, L"https://www.wjx.cn/vm/mqNTTRL.aspx#", 0, 0, SW_SHOW); break;
					case HomeAction::Contact:
						ShellExecuteW(0, 0, L"mailto:alan-crl@foxmail.com", 0, 0, SW_SHOW); break;
					default: break;
					}
					break;
				}

				// 语言
				case settingTabEnum::Language:
				{
					Design::PageHeader(IA(I18nKey.SettingsUI.Language.N).c_str());
					Design::SectionHeader(IA(I18nKey.SettingsUI.Language.UI.N).c_str());

					{
						vector<string> languages{
							IA(I18nKey.SettingsUI.Language.UI.Language.en_US),
							IA(I18nKey.SettingsUI.Language.UI.Language.zh_CN),
							IA(I18nKey.SettingsUI.Language.UI.Language.zh_TW)
						};
						const int previousLanguage = SelectLanguage;
						if (Design::ComboRow("##language-ui",
							IA(I18nKey.SettingsUI.Language.UI.Select).c_str(),
							IA(I18nKey.SettingsUI.Language.UI.SelectE).c_str(), "\ue774",
							SelectLanguage, languages)
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
					}
					break;
				}
				// 配置保存
				case settingTabEnum::tabConfiguration:
				{
					Design::PageHeader(IA(I18nKey.SettingsUI.Design.GeneralStorage).c_str());
					Design::SectionHeader(IA(I18nKey.SettingsUI.Configuration.Clean.N).c_str());

					{
						Design::ToggleRow("##configuration-clean",
							IA(I18nKey.SettingsUI.Configuration.Clean.Enable).c_str(),
							IA(I18nKey.SettingsUI.Configuration.Clean.EnableE).c_str(), "\ue74d",
							ConfigurationSetting.Enable);
						if (Inkeys::config.Config.AutoClean != ConfigurationSetting.Enable)
						{
							Inkeys::config.Config.AutoClean = ConfigurationSetting.Enable;
							QueueConfigWrite();
							WriteSetting();
						}
					}

					Design::SectionHeader(
						IA(I18nKey.SettingsUI.Configuration.CanvasSave.N).c_str());
					{
						Design::ToggleRow("##configuration-history",
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.Enable).c_str(),
							IA(I18nKey.SettingsUI.Configuration.CanvasSave.EnableE).c_str(), "\ue81c",
							SaveSetting.Enable);
						if (setlist.saveSetting.enable != SaveSetting.Enable)
						{
							setlist.saveSetting.enable = SaveSetting.Enable;
							SyncDraw3State();
							WriteSetting();
						}
					}

					break;
				}
				// 软件版本
				case settingTabEnum::tab6:
				{
					Design::PageHeader(IA(I18nKey.SettingsUI.Version.N).c_str());

					if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNew
						&& Design::ButtonRow("##version-manual-update", IA(I18nKey.SettingsUI.Version.ManualUpdate.N).c_str(),
							nullptr, "\ue895", IA(I18nKey.SettingsUI.Version.ManualUpdate.ManualUpdate).c_str()))
					{
						mandatoryUpdate = true;
						AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
					}
					if (inconsistentArchitecture)
					{
						Design::Notice("##version-architecture-warning",
							IA(I18nKey.SettingsUI.Version.N).c_str(),
							IA(I18nKey.SettingsUI.Version.VersionTip).c_str(),
							ImFluentInfoSeverity_Warning);
					}

					Design::SectionHeader(IA(I18nKey.SettingsUI.Version.Info.N).c_str());
					const bool versionCardVisible = Design::BeginCard("##version-info",
						{ 0.0F, 0.0F }, ImFluentCardStyle_Filled);
					if (versionCardVisible)
					{
						const bool sideBySide = TextureSettingSign[1]
							&& ImGui::GetContentRegionAvail().x >= Design::Pixels(480.0F);
						const float logoWidth = min(Design::Pixels(96.0F),
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
						// 宽卡将原图片与版本文字并排，窄卡按阅读顺序排列并保留原比例。
						if (sideBySide) ImGui::SameLine(0.0F, Design::Pixels(24.0F));
						ImGui::BeginGroup();
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
						Design::Text(versionTextUtf8.c_str());
						if (settingCICD.url.empty())
						{
							Design::Text(IA(I18nKey.SettingsUI.Version.Info.ManualBuild).c_str(), ImFluentTextStyle_Caption, Design::TextSecondary);
						}
						else
						{
							Design::Text(IA(I18nKey.SettingsUI.Version.Info.AutoBuild).c_str(), ImFluentTextStyle_Caption, Design::TextSecondary);
							if (Design::HyperlinkButton(
								IA(I18nKey.SettingsUI.Version.Info.CICDInfo).c_str()))
								settingTab = settingTabEnum::tabCICD;
						}
						ImGui::EndGroup();
					}
					Design::EndCard();

					Design::SectionHeader(
						IA(I18nKey.SettingsUI.Version.UserInfo.N).c_str());
					const string userIdDescription = IA(I18nKey.SettingsUI.Version.UserInfo.UserId)
						+ " " + utf16ToUtf8(userId);
					{
						if (Design::ButtonRow("##version-user-id",
							IA(I18nKey.SettingsUI.Version.UserInfo.CopyUserId).c_str(),
							userIdDescription.c_str(), "\ue8c8",
							IA(I18nKey.Operate.Copy).c_str()))
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
					}

					Design::SectionHeader(IA(I18nKey.SettingsUI.Version.Repair.N).c_str());
					{
						if (Design::ButtonRow("##version-repair",
							IA(I18nKey.SettingsUI.Version.Repair.RepairSoftware).c_str(),
							IA(I18nKey.SettingsUI.Version.Repair.RepairSoftwareE).c_str(), "\ue90f",
							IA(I18nKey.Operate.Repair).c_str()))
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
					}
					{
						Design::ToggleRow("##version-repair-architecture",
							IA(I18nKey.SettingsUI.Version.Repair.RepairArch).c_str(),
							nullptr, "\ue8a9",
							EnableFixWithChangeArchitecture);
					}

					Design::SectionHeader(IA(I18nKey.SettingsUI.Version.Update.N).c_str());
					{
						// 原固定底栏的状态和动作移到版本页，仍使用发布版状态机和 FIFO。
						using enum AutomaticUpdateStateEnum;
						const auto updateState = AutomaticUpdateState;
						const char* statusKey = I18nKey.SettingsUI.Update.NotStarted;
						ImFluentInfoSeverity severity = ImFluentInfoSeverity_Informational;
						bool manualDownload = false;
						switch (updateState)
						{
						case UpdateNotStarted: severity = ImFluentInfoSeverity_Warning; manualDownload = true; break;
						case UpdateObtainInformation: statusKey = I18nKey.SettingsUI.Update.ObtainInformation; manualDownload = true; break;
						case UpdateInformationFail: statusKey = I18nKey.SettingsUI.Update.InformationFail; severity = ImFluentInfoSeverity_Critical; manualDownload = true; break;
						case UpdateInformationDamage: statusKey = I18nKey.SettingsUI.Update.InformationDamage; severity = ImFluentInfoSeverity_Critical; manualDownload = true; break;
						case UpdateInformationUnStandardized: statusKey = I18nKey.SettingsUI.Update.InformationUnStandardized; severity = ImFluentInfoSeverity_Critical; manualDownload = true; break;
						case UpdateDownloading: statusKey = I18nKey.SettingsUI.Update.Downloading; break;
						case UpdateDownloadFail: statusKey = I18nKey.SettingsUI.Update.DownloadFail; severity = ImFluentInfoSeverity_Critical; manualDownload = true; break;
						case UpdateDownloadDamage: statusKey = I18nKey.SettingsUI.Update.DownloadDamage; severity = ImFluentInfoSeverity_Critical; manualDownload = true; break;
						case UpdateRestart: statusKey = I18nKey.SettingsUI.Update.Restart; break;
						case UpdateLatest: statusKey = I18nKey.SettingsUI.Update.Latest; severity = ImFluentInfoSeverity_Success; break;
						case UpdateNewer: statusKey = I18nKey.SettingsUI.Update.Newer; severity = ImFluentInfoSeverity_Success; break;
						case UpdateNew: statusKey = I18nKey.SettingsUI.Update.New; severity = ImFluentInfoSeverity_Warning; break;
						}
						string status = IA(statusKey);
						if (updateState == UpdateDownloading)
						{
							try { status = vformat(status, make_format_args(downloadLine)); }
							catch (...) {}
							const double downloaded = static_cast<double>(downloadNewProgramState.downloadedSize.load());
							const double size = static_cast<double>(downloadNewProgramState.fileSize.load());
							if (size > 0.0 && downloaded <= size) status += format(" {:.1f}%", downloaded / size * 100.0);
						}
						if (updateState == UpdateLatest || updateState == UpdateNewer)
						{
							const string channel = GetUpdateChannel();
							const char* channelKey = channel == "LTS" ? I18nKey.SettingsUI.Update.Channel.LTS
								: channel == "Insider" ? I18nKey.SettingsUI.Update.Channel.Insider
								: channel == "Dev" ? I18nKey.SettingsUI.Update.Channel.Dev
								: channel == "Canary" ? I18nKey.SettingsUI.Update.Channel.Canary : I18nKey.SettingsUI.Update.Channel.Other;
							status += " (" + IA(channelKey) + ")";
						}
						const auto unavailableInformation = [&]
						{
							QueueInformation(I18n::getWOr(I18nKey.Dialogs.UpdateModuleUnavailable.Body,
								L"The automatic update module is not active, which indicates that this is not an official release.\nGo to Software Version and select Repair Software."));
						};
						Design::Notice("##update-status", IA(I18nKey.SettingsUI.Version.Update.N).c_str(), status.c_str(), severity);
						const string check = IA(I18nKey.SettingsUI.Update.Check);
						const string manual = IA(I18nKey.SettingsUI.Update.ManualDownload);
						if (updateState == UpdateNotStarted)
						{
							const int action = Design::ButtonGroupRow("##update-actions", IA(I18nKey.SettingsUI.Update.Check).c_str(),
								nullptr, "\ue895", { { "repair", IA(I18nKey.SettingsUI.Update.Repair) }, { "manual", manual }, { "check", check } });
							if (action == 0 || action == 2) unavailableInformation();
							else if (action == 1) ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
						}
						else if (manualDownload)
						{
							const int action = Design::ButtonGroupRow("##update-actions", IA(I18nKey.SettingsUI.Update.Check).c_str(),
								nullptr, "\ue895", { { "manual", manual }, { "check", check } });
							if (action == 0) ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							else if (action == 1) AutomaticUpdateState = UpdateObtainInformation;
						}
						else if (updateState == UpdateNew)
						{
							const int action = Design::ButtonGroupRow("##update-actions", IA(I18nKey.SettingsUI.Update.Check).c_str(),
								nullptr, "\ue895", { { "update", IA(I18nKey.SettingsUI.Update.UpdateNow), true }, { "check", check } });
							if (action >= 0)
							{
								if (action == 0) mandatoryUpdate = true;
								AutomaticUpdateState = UpdateObtainInformation;
							}
						}
						else if (Design::ButtonRow("##update-check", check.c_str(), nullptr, "\ue895", check.c_str()))
							AutomaticUpdateState = UpdateObtainInformation;
					}
					{
						const bool previousAutoUpdate = EnableAutoUpdate;
						Design::ToggleRow("##version-auto-update",
							IA(I18nKey.SettingsUI.Version.Update.AutoUpate).c_str(),
							nullptr, "\ue895",
							EnableAutoUpdate);
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
					}
					Design::Notice("##version-channel-notice",
						IA(I18nKey.SettingsUI.Version.Update.Channel.N).c_str(),
						IA(I18nKey.SettingsUI.Version.Update.ChannelTip).c_str(),
						ImFluentInfoSeverity_Informational);
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
						if (Design::ComboRow("##version-update-channel",
							IA(I18nKey.SettingsUI.Version.Update.Channel.N).c_str(),
							nullptr, "\ue713",
							channelIndex, channels) && previousChannelIndex != channelIndex)
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
					}
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
						if (Design::ComboRow("##version-update-architecture",
							IA(I18nKey.SettingsUI.Version.Update.Arch.N).c_str(),
							IA(I18nKey.SettingsUI.Version.Update.Arch.E).c_str(), "\ue8a9",
							architectureIndex, architectures)
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
					}
					break;
				}

				// CI/CD 构建详情
				case settingTabEnum::tabCICD:
				{
					if (Design::IconButton("##cicd-back", "\ue72b"))
						settingTab = settingTabEnum::tab6;
					ImGui::SameLine(0.0F, Design::Pixels(8.0F));
					if (Design::HyperlinkButton(IA(I18nKey.SettingsUI.Version.N).c_str()))
						settingTab = settingTabEnum::tab6;
					Design::PageHeader(IA(I18nKey.SettingsUI.CICD.N).c_str(), "CI/CD");
					Design::PageContentStart();

					// 链接作为可导航设置行，长URL沿说明区换行，不挤出按钮边界。
					if (Design::NavigationRow("##cicd-run-link", "CI/CD",
						utf16ToUtf8(settingCICD.url).c_str(), "\ue71b"))
						ShellExecuteW(nullptr, nullptr, settingCICD.url.c_str(), nullptr, nullptr, SW_SHOW);
					if (Design::NavigationRow("##cicd-repository-link",
						IA(I18nKey.SettingsUI.CICD.Repository).c_str(),
						utf16ToUtf8(settingCICD.repoUrl).c_str(), "\ue8a5"))
						ShellExecuteW(nullptr, nullptr, settingCICD.repoUrl.c_str(), nullptr, nullptr, SW_SHOW);
					Design::PageContentStart();
					const bool cicdCardVisible = Design::BeginCard("##cicd-details");
					if (cicdCardVisible)
					{

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
						Design::Text(detailsUtf8.c_str());
					}
					Design::EndCard();
					break;
				}

				// ---------------------

				// 常规
				case settingTabEnum::tab2:
				{
					Inkeys::UI::Setting::Design::GeneralDraft draft{
						StartUp, BarZoom, SettingGlobalScale, Experimental.Inkeys3.EdgeLightingEnable,
						TopSleepTime, RightClickClose, RegularSetting.TeachingSafetyMode,
						Experimental.Inkeys3.StartupPreviewPreferenceState.ConfiguredEnabled(),
						Experimental.Inkeys3.AnimationEnable, Experimental.Inkeys3.AnimationSpeedRate,
						Experimental.Inkeys3.DynamicEdgeLighting };
					const auto events = Inkeys::UI::Setting::Design::RenderGeneral(draft, destination.section);
					// 页面只返回一次交互结果，所有副作用继续留在既有 FIFO/配置接线域。
					if (destination.section == Design::GeneralSection::Basic)
					{
						StartUp = draft.startup;
						if (events.startupChanged && setlist.startUp != StartUp)
						{
							SetStartupState(StartUp, GetCurrentExePath(), L"$Inkeys");
							setlist.startUp = StartUp;
							WriteSetting();
						}
						if (events.createShortcut)
						{
							wchar_t desktopPath[MAX_PATH];
							if (SHGetSpecialFolderPathW(0, desktopPath, CSIDL_DESKTOP, FALSE))
							{
								SettingBusinessCommand command;
								command.kind = SettingBusinessKind::CreateShortcut;
								command.text = wstring(desktopPath) + L"\\" + IW(I18nKey.Widget.LnkName) + L".lnk";
								command.directory = GetCurrentExePath();
								QueueBusiness(std::move(command));
							}
						}
						if (events.shortcutOptions)
						{
							settingPlugInTab = settingPlugInTabEnum::tabPlug3;
							settingTab = settingTabEnum::tab4;
						}
						if (events.startupPreviewChanged
							&& Experimental.Inkeys3.StartupPreviewPreferenceState.SetConfigured(draft.startupPreview))
						{
							Inkeys::config.Experimental.Inkeys3.UI3.StartupPreview.Enable = draft.startupPreview;
							if (Experimental.Inkeys3.StartupPreviewPreferenceState.ConsumeWritePending())
								QueueConfigWrite();
						}
					}
					if (destination.section == Design::GeneralSection::Appearance)
					{
						Experimental.Inkeys3.AnimationEnable = draft.animation;
						Experimental.Inkeys3.AnimationSpeedRate = draft.animationRate;
						if (events.animationChanged)
						{
							if (!draft.animation) Design::SnapNavigationAnimations(navigationState);
							Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable = draft.animation;
							Inkeys::UI::Bar::SetAnimationOptions(draft.animation, draft.animationRate);
							QueueConfigWrite();
						}
						if (draft.animation && fabs(draft.animationRate - static_cast<float>(
							Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate.load())) > 0.0001F)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate = static_cast<double>(draft.animationRate);
							Inkeys::UI::Bar::SetAnimationOptions(draft.animation, draft.animationRate);
							Experimental.Inkeys3.AnimationSpeedSavePending = true;
						}
						if (!events.animationRateActive && Experimental.Inkeys3.AnimationSpeedSavePending)
						{
							QueueConfigWrite();
							Experimental.Inkeys3.AnimationSpeedSavePending = false;
						}
						Experimental.Inkeys3.DynamicEdgeLighting = draft.dynamicEdgeLighting;
						if (events.dynamicEdgeLightingChanged)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic = draft.dynamicEdgeLighting;
							Inkeys::UI::Bar::SetEdgeLightingOptions(draft.edgeLighting, draft.dynamicEdgeLighting);
							QueueConfigWrite();
						}
						BarZoom = draft.barZoom;
						if (fabs(BarZoom - static_cast<float>(Inkeys::config.UI.Bar.Zoom.load())) > 0.0001F)
						{
							Inkeys::config.UI.Bar.Zoom = static_cast<double>(BarZoom);
							Inkeys::UI::Bar::SetConfigZoom(static_cast<double>(BarZoom));
							BarZoomSavePending = true;
						}
						if (!events.barZoomActive && BarZoomSavePending)
						{
							QueueConfigWrite();
							BarZoomSavePending = false;
						}
						Experimental.Inkeys3.EdgeLightingEnable = draft.edgeLighting;
						if (events.edgeLightingChanged && Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable != draft.edgeLighting)
						{
							Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable = draft.edgeLighting;
							Inkeys::UI::Bar::SetEdgeLightingOptions(draft.edgeLighting, Experimental.Inkeys3.DynamicEdgeLighting);
							QueueConfigWrite();
						}
						SettingGlobalScale = draft.settingZoom;
						if (!events.settingZoomActive) commitSettingScale();
					}
					if (destination.section == Design::GeneralSection::Interaction)
					{
						TopSleepTime = draft.topInterval;
						if (events.topIntervalChanged && setlist.topSleepTime != TopSleepTime)
						{
							setlist.topSleepTime = TopSleepTime;
							WriteSetting();
							topWindowNow = true;
						}
						RightClickClose = draft.rightClickClose;
						if (events.rightClickCloseChanged && setlist.RightClickClose != RightClickClose)
						{
							setlist.RightClickClose = RightClickClose;
							WriteSetting();
						}
						RegularSetting.TeachingSafetyMode = draft.safetyMode;
						if (events.safetyModeChanged && setlist.regularSetting.teachingSafetyMode != draft.safetyMode)
						{
							setlist.regularSetting.teachingSafetyMode = draft.safetyMode;
							WriteSetting();
							CrashHandler::SetFlag(setlist.regularSetting.teachingSafetyMode);
						}
					}
					break;
				}

				// 绘制
				case settingTabEnum::tab3:
				{
					Design::PageHeader(IA(I18nKey.SettingsUI.Draw.N).c_str());

					Design::SectionHeader(IA(I18nKey.SettingsUI.Draw.Effect.N).c_str());
					{
						vector<string> devices{
							IA(I18nKey.SettingsUI.Draw.Effect.Device.Touch),
							IA(I18nKey.SettingsUI.Draw.Effect.Device.MousePen)
						};
						const int previousPaintDevice = PaintDevice;
						if (Design::ComboRow("##draw-device",
							IA(I18nKey.SettingsUI.Draw.Effect.Device.N).c_str(),
							IA(I18nKey.SettingsUI.Draw.Effect.Device.E).c_str(), "\ue7f8",
							PaintDevice, devices)
							&& previousPaintDevice != PaintDevice
							&& setlist.paintDevice != PaintDevice)
						{
							setlist.paintDevice = PaintDevice;
							SyncDraw3State();
							WriteSetting();
							drawingScale = GetDrawingScale();
							stopTimingError = GetStopTimingError();
						}
					}

					Design::SectionHeader(IA(I18nKey.SettingsUI.Draw.Eraser.N).c_str());
					// 五入口与总开关使用同一快照；关闭自动粗细不会覆盖各入口偏好。
					const auto eraserPreferences = EraserPreferencesSnapshot();
					bool automatic = eraserPreferences.automaticEnabled;
					if (Design::ToggleRow("##eraser-automatic", IA(I18nKey.SettingsUI.Draw.Eraser.Automatic).c_str(),
						nullptr, "\ued62", automatic))
						SetGlobalEraserPreference(-1, -1, automatic ? 1 : 0);
					const string entries[]{
						IA(I18nKey.SettingsUI.Draw.Eraser.Input.MouseLeft), IA(I18nKey.SettingsUI.Draw.Eraser.Input.MouseRight),
						IA(I18nKey.SettingsUI.Draw.Eraser.Input.Touch), IA(I18nKey.SettingsUI.Draw.Eraser.Input.PenTip),
						IA(I18nKey.SettingsUI.Draw.Eraser.Input.PenTail) };
					const vector<string> kinds{ IA(I18nKey.SettingsUI.Draw.Eraser.Kind.Fixed), IA(I18nKey.SettingsUI.Draw.Eraser.Kind.Speed) };
					const vector<string> responses{ IA(I18nKey.SettingsUI.Draw.Eraser.Response.Auto),
						IA(I18nKey.SettingsUI.Draw.Eraser.Response.ScreenPen), IA(I18nKey.SettingsUI.Draw.Eraser.Response.Digitizer) };
					for (int entry = 0; entry < 5; ++entry)
					{
						ImGui::PushID(entry);
						int kind = static_cast<int>(eraserPreferences.entries[entry].kind);
						if (Design::ComboRow("##eraser-kind", entries[entry].c_str(), nullptr, "\ued62", kind, kinds))
						{
							SetEraserInputPreference(entry, kind);
							QueueConfigWrite();
						}
						if (entry >= 3 && kind == 1)
						{
							const int first = AutomaticPenResponseAvailable() ? 0 : 1;
							const vector<string> availableResponses(responses.begin() + first, responses.end());
							int response = static_cast<int>(eraserPreferences.entries[entry].penResponse) - first;
							if (Design::ComboRow("##eraser-response", IA(I18nKey.SettingsUI.Draw.Eraser.ResponseFor).c_str(),
								nullptr, "\ue7f8", response, availableResponses))
							{
								// Win7 隐藏 Auto 项后，仍保存原始稳定枚举，不能保存可见下标。
								SetEraserInputPreference(entry, kind, response + first);
								QueueConfigWrite();
							}
						}
						ImGui::PopID();
					}
					{
						auto options = Inkeys::Drawing::Draw3::ProductHost().EraserDevelopmentOptions();
						if (Design::ToggleRow("##eraser-touch-area", IA(I18nKey.SettingsUI.Draw.Eraser.TouchArea.N).c_str(),
							IA(I18nKey.SettingsUI.Draw.Eraser.TouchArea.E).c_str(), "\ue7c9", options.touchContactAreaAssistance))
						{
							Inkeys::Drawing::Draw3::ProductHost().SetEraserDevelopmentOptions(options);
							Inkeys::config.Experimental.Inkeys3.Draw3.TouchContactAreaAssistance = options.touchContactAreaAssistance;
							QueueConfigWrite();
						}
					}
					break;
				}

				// 插件
				case settingTabEnum::tab4:
				{
					const auto renderPluginHeader = [&](const char* title,
						const char* subtitle, const wchar_t* sourceUrl = nullptr)
						{
							if (Design::IconButton("##plugin-back", "\ue72b"))
								settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							ImGui::SameLine(0.0F, Design::Pixels(8.0F));
							if (Design::HyperlinkButton(IA(I18nKey.SettingsUI.PlugIn.N).c_str()))
								settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							Design::PageHeader(title, subtitle);
							if (sourceUrl)
							{
								if (Design::HyperlinkButton("GitHub"))
									ShellExecuteW(nullptr, nullptr, sourceUrl, nullptr, nullptr, SW_SHOW);
							}
							Design::PageContentStart();
						};
					const auto renderPluginOverviewCard = [&](const char* id,
						const char* title, const string& status, const char* description,
						const char* glyph, int target)
						{
							string overviewDescription = status;
							if (!overviewDescription.empty() && description && *description)
								overviewDescription += "\n";
							if (description) overviewDescription += description;
							if (Design::NavigationRow(id, title, overviewDescription.c_str(), glyph))
								settingPlugInTab = target;
						};
					const auto renderToggleCard = [&](const char* id, const char* title,
						const char* description, const char* glyph, bool& value, auto&& changed)
						{
							if (Design::ToggleRow(id, title, description, glyph, value))
								changed(value);
						};

					switch (settingPlugInTab)
					{
					case settingPlugInTabEnum::tabPlug1:
					{
						Design::PageHeader(IA(I18nKey.SettingsUI.PlugIn.N).c_str());
						Design::PageContentStart();
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
							Design::Notice("##ppt-version-error",
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.VersionError).c_str(),
								error.c_str(),
								ImFluentInfoSeverity_Warning);
						}
						if (Design::Notice("##ppt-setup-notice",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tip).c_str(),
							ImFluentInfoSeverity_Informational,
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Solve).c_str()))
							ShellExecuteW(nullptr, nullptr, L"https://www.inkeys.top/tutorial/ppt-com",
								nullptr, nullptr, SW_SHOW);
						if (pptComSetlist.setAdmin)
						{
							if (Design::Notice("##ppt-admin-notice",
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str(),
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Warn).c_str(),
								ImFluentInfoSeverity_Warning,
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Solve).c_str()))
								ShellExecuteW(nullptr, nullptr, L"https://www.inkeys.top/tutorial/ppt-admin",
									nullptr, nullptr, SW_SHOW);
						}

						Design::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.N).c_str());
						renderToggleCard("##ppt-loading-screen",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.LoadPage).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.LoadPageE).c_str(),
							"\ue895", PptComShowLoadingScreen, [&](bool value)
							{
								pptComSetlist.showLoadingScreen = value;
								PptComWriteSetting();
							});

						Design::SectionHeader(
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

						Design::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.N).c_str());
						{
							if (Design::ButtonRow("##ppt-reset-position",
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.Reset).c_str(),
								nullptr, "\ue777",
								IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset).c_str()))
							{
								Inkeys::UI::Ppt::ResetPositions();
								PptComWriteSetting();
							}
						}
						renderToggleCard("##ppt-remember-position",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.Remember).c_str(),
							nullptr, "\ue8f1", MemoryWidgetPosition, [&](bool value)
							{
								pptComSetlist.memoryWidgetPosition = value;
								PptComWriteSetting();
							});

						Design::SectionHeader(
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.N).c_str());
						bool anyScaleActive = false;
						bool bottomSyncEnabled = false;
						bool middleSyncEnabled = false;
						bool bottomEdited = false;
						bool middleEdited = false;
						const auto renderScaleCard = [&](const char* id, const char* title,
							float& value, bool& unified, bool& syncEnabled, bool& edited)
							{
								const string resetLabel = IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset);
								const string syncLabel = IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Sync);
								const string indicator = vformat(
									IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind),
									make_format_args(value));
								const float resetWidth = Design::ButtonWidth(resetLabel.c_str());
								const float syncWidth = Design::ButtonWidth(syncLabel.c_str());
								const float requestedWidth = max(240.0F, resetWidth + syncWidth + 8.0F);
								const auto measure = Design::MeasureRow(ImGui::GetContentRegionAvail().x / Design::Pixels(1.0F),
									requestedWidth, 72.0F);
								const bool wrapButtons = resetWidth + syncWidth + 8.0F > measure.actionWidth;
								Design::SettingRow(id, title, indicator.c_str(), "\ue9a6", requestedWidth,
									wrapButtons ? 112.0F : 72.0F, [&](const Inkeys::UI::Setting::LayoutRect& bounds)
									{
										// 活动状态在slider提交后立刻捕获，后续按钮不能覆盖松手保存判定。
										ImGui::SetNextItemWidth(bounds.Width());
										edited = Design::Slider("##scale", &value, 0.5F, 3.0F, "%.2f");
										value = round(value * 100.0F) / 100.0F;
										anyScaleActive = anyScaleActive || ImGui::IsItemActive();
										const float actualResetWidth = min(bounds.Width(), Design::Pixels(resetWidth));
										const float actualSyncWidth = min(bounds.Width(), Design::Pixels(syncWidth));
										const float resetX = wrapButtons ? bounds.right - actualResetWidth
											: bounds.right - actualResetWidth - Design::Pixels(8.0F) - actualSyncWidth;
										ImGui::SetCursorScreenPos({ resetX, bounds.top + Design::Pixels(40.0F) });
										if (Design::Button((resetLabel + "###reset").c_str(), { actualResetWidth, Design::Pixels(32.0F) }))
										{
											value = 1.0F;
											edited = true;
										}
										ImGui::SetCursorScreenPos({ bounds.right - actualSyncWidth,
											bounds.top + Design::Pixels(wrapButtons ? 80.0F : 40.0F) });
										const bool previousUnified = unified;
										Design::ToggleButton((syncLabel + "###sync").c_str(), &unified,
											{ actualSyncWidth, Design::Pixels(32.0F) });
										syncEnabled = !previousUnified && unified;
									});
							};
						renderScaleCard("##ppt-bottom-scale",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.BottomSideBoth).c_str(),
							BottomSideBothWidgetScale, BottomSideBothWidgetScaleUnifie,
							bottomSyncEnabled, bottomEdited);
						renderScaleCard("##ppt-middle-scale",
							IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.MiddleSideBoth).c_str(),
							MiddleSideBothWidgetScale, MiddleSideBothWidgetScaleUnifie,
							middleSyncEnabled, middleEdited);

						const auto linkedScale = Inkeys::UI::Setting::ResolveLinkedPptScaleDraft(
							{ BottomSideBothWidgetScale, MiddleSideBothWidgetScale,
								BottomSideBothWidgetScaleUnifie, MiddleSideBothWidgetScaleUnifie },
							bottomEdited, middleEdited, bottomSyncEnabled, middleSyncEnabled);
						BottomSideBothWidgetScale = linkedScale.bottom;
						MiddleSideBothWidgetScale = linkedScale.middle;
						// 发布版拖动/重置/同步立即应用；写盘只在所有滑块松手后进入 FIFO。
						if (pptComSetlist.bottomSideBothWidgetScale != BottomSideBothWidgetScale)
						{
							pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
							Inkeys::UI::Ppt::NotifyConfigurationChanged(Inkeys::UI::Ppt::ConfigGroup::BottomPair);
						}
						if (pptComSetlist.middleSideBothWidgetScale != MiddleSideBothWidgetScale)
						{
							pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
							Inkeys::UI::Ppt::NotifyConfigurationChanged(Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
						}
						if (!anyScaleActive)
						{
							if (BottomSideBothWidgetScaleRecord != BottomSideBothWidgetScale)
							{
								pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
								BottomSideBothWidgetScaleRecord = BottomSideBothWidgetScale;
								PptComWriteSetting();
								Inkeys::UI::Ppt::NotifyConfigurationChanged(Inkeys::UI::Ppt::ConfigGroup::BottomPair);
							}
							if (MiddleSideBothWidgetScaleRecord != MiddleSideBothWidgetScale)
							{
								pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
								MiddleSideBothWidgetScaleRecord = MiddleSideBothWidgetScale;
								PptComWriteSetting();
								Inkeys::UI::Ppt::NotifyConfigurationChanged(Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
							}
						}

						Design::SectionHeader(
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
						Design::Notice("##super-top-notice",
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.N).c_str(),
							IA(I18nKey.SettingsUI.PlugIn.SuperTop.Warn).c_str(),
							ImFluentInfoSeverity_Warning);
						Design::SectionHeader(
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
						Design::SectionHeader(
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
						Design::SectionHeader(
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
						Design::Notice("##ddb-license-notice",
							"GPLv3 开源插件",
							"插件仅供学习交流和研究使用，不得用于其他任何用途。\n同类软件悬浮窗拦截助手(DesktopDrawpadBlocker)是依据 GPLv3 许可协议发布的开源软件。\n并在 Github 仓库得到发布：https://github.com/Alan-CRL/DesktopDrawpadBlocker\n我们发布这款程序，希望它有用，但不承诺任何质量保证责任。\n用户在使用该插件时，需自行承担由此产生的后果和影响，使用插件则视为同意此协议。",
							ImFluentInfoSeverity_Warning);

						Design::SectionHeader("使用插件");
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

						Design::SectionHeader("常规选项");
						renderToggleCard("##ddb-admin", "以管理员身份启动插件",
							"切换后软件将阻塞至多拦截间隔的秒数，以等待插件的响应。", "\ue72e",
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
						{
							int intervalIndex = ddbInteractionSetList.sleepTime == 500 ? 0
								: (ddbInteractionSetList.sleepTime == 1000 ? 1
									: (ddbInteractionSetList.sleepTime == 3000 ? 2
										: (ddbInteractionSetList.sleepTime == 10000 ? 4 : 3)));
							const int previousIntervalIndex = intervalIndex;
							vector<string> intervals{
								"短（500ms）", "较短（1s）", "中等（3s）", "较长（5s）", "长（10s）"
							};
							if (Design::ComboRow("##ddb-interval",
								"拦截间隔",
								"更短的间隔可以更快拦截出现的窗口，但会占用更多的CPU资源。", "\ue916",
								intervalIndex, intervals)
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
						}

						Design::SectionHeader("精确控制");
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
							{ "##ddb-seewo5c", "希沃轻白板（5C） 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.SeewoWhiteboard5CFloating },
							{ "##ddb-pinco-sidebar", "希沃品课教师端 侧栏悬浮窗", nullptr,
								&Ddb.intercept.SeewoPincoSideBarFloating },
							{ "##ddb-pinco-drawing", "希沃品课教师端 桌面画笔悬浮窗", "包括PPT控件。",
								&Ddb.intercept.SeewoPincoDrawingFloating },
							{ "##ddb-seewo-ppt", "希沃PPT小工具", nullptr,
								&Ddb.intercept.SeewoPPTFloating },
							{ "##ddb-iwb-assistant", "希沃课堂助手 PPT小工具", nullptr,
								&Ddb.intercept.SeewoIwbAssistantFloating },
							{ "##ddb-yiou", "欧帝白板 桌面画笔悬浮窗", "包括PPT控件。",
								&Ddb.intercept.YiouBoardFloating },
							{ "##ddb-aiclass", "AiClass 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.AiClassFloating },
							{ "##ddb-classinx", "ClassIn X 桌面画笔悬浮窗", nullptr,
								&Ddb.intercept.ClassInXFloating },
							{ "##ddb-intelligent-class", "天喻教育云互动课堂 桌面画笔悬浮窗", "包括PPT控件。", &Ddb.intercept.IntelligentClassFloating },
							{ "##ddb-changyan4", "畅言智慧课堂4.0 桌面画笔悬浮窗", "包括PPT控件。支持在白板时自动恢复。需要DDB管理员权限。",
								&Ddb.intercept.ChangYanFloating },
							{ "##ddb-changyan5", "畅言智慧课堂5.0 桌面画笔悬浮窗", "包括PPT控件。支持在白板时自动恢复。需要DDB管理员权限。",
								&Ddb.intercept.ChangYan5Floating },
							{ "##ddb-iclass30-sidebar", "C30智能教学 侧栏悬浮窗", "需要DDB管理员。", &Ddb.intercept.Iclass30SidebarFloating },
							{ "##ddb-iclass30-drawing", "C30智能教学 桌面画笔悬浮窗", "包括PPT控件。支持在白板时自动恢复，支持窗口追踪。需要DDB管理员权限。",
								&Ddb.intercept.Iclass30Floating },
							{ "##ddb-seewo-desktop-sidebar", "希沃桌面 侧栏悬浮窗", "需要DDB管理员。1.0/2.0/2.5/3.0 普教版/高教版 通用",
								&Ddb.intercept.SeewoDesktopSideBarFloating },
							{ "##ddb-seewo-desktop-drawing", "希沃桌面 桌面画笔悬浮窗", "需要DDB管理员。1.0/2.0/2.5/3.0 普教版/高教版 通用",
								&Ddb.intercept.SeewoDesktopDrawingFloating }
						};
						bool interceptChanged = false;
						for (const auto& row : interceptRows)
						{
							if (Design::ToggleRow(row.id, row.title, row.description, "\ue7ba", *row.value))
								interceptChanged = true;
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
					Design::PageHeader("组件");
					auto renderComponentToggle = [&](const char* id, const char* title,
						const char* description, const char* glyph, bool& value, auto& stored)
					{
						Design::ToggleRow(id, title, description, glyph, value);
						if (stored != value)
						{
							stored = value;
							WriteSetting();
							SyncUi3BuiltInComponents();
						}
					};
					auto componentSection = [&](const char* title)
					{
						Design::SectionHeader(title);
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
						"需要安装 ClassIsland 插件 IslandCaller，并需要 ClassIsland 注册 Url 协议。",
						"\ue716", ComponentShortcutButtonRollCallIslandCaller1,
						setlist.component.shortcutButton.rollCall.IslandCaller1);
					renderComponentToggle("component-island-caller-2",
						"IslandCaller 2 单人点名",
						"需要安装 ClassIsland 插件 IslandCaller，并需要 ClassIsland 注册 Url 协议。",
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
					Design::Notice("##classisland-protocol-notice",
						"ClassIsland",
						"需要在 ClassIsland 应用设置 中注册 Url 导航协议。",
						ImFluentInfoSeverity_Informational);
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
						"在 ClassIsland 当前没有加载课表时，此组件不起作用。", "\ue8d7",
						ComponentShortcutButtonLinkageClassislandClassswap,
						setlist.component.shortcutButton.linkage.classislandClassswap);
					break;
				}
				case settingTabEnum::tabExperimental:
				{
					Design::PageHeader("实验室");
					Design::SectionHeader("Inkeys3");
					if (Design::ToggleRow("##experimental-dcomp", "在支持的设备上启动 DComp",
						"重启软件后生效；关闭后使用 ULW 进行测试。", "\ue7f4", Experimental.Inkeys3.EnableDirectComposition))
					{
						Inkeys::config.Experimental.Inkeys3.Draw3.EnableDirectComposition = Experimental.Inkeys3.EnableDirectComposition;
						QueueConfigWrite();
					}
					if (Design::ToggleRow("##experimental-dirty-debug", "脏区调试",
						"显示 UI3 每帧实际提交的脏区边界。", "\ue90f", Experimental.Inkeys3.DebugMode))
					{
						Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable = Experimental.Inkeys3.DebugMode;
						Inkeys::UI::Bar::SetDebugOptions(Experimental.Inkeys3.DebugMode, Experimental.Inkeys3.ShowFrameRate);
						QueueConfigWrite();
					}
					if (Experimental.Inkeys3.DebugMode
						&& Design::ToggleRow("##experimental-frame-rate", "显示帧率",
							"每秒更新上一秒平均帧率和无等待帧率。", "\ue9d9", Experimental.Inkeys3.ShowFrameRate))
					{
						Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate = Experimental.Inkeys3.ShowFrameRate;
						Inkeys::UI::Bar::SetDebugOptions(Experimental.Inkeys3.DebugMode, Experimental.Inkeys3.ShowFrameRate);
						QueueConfigWrite();
					}
					break;
				}
				case settingTabEnum::tab8:
				{
					Design::PageHeader(IA(I18nKey.SettingsUI.Sponsor.N).c_str());
					Design::PageContentStart();
					const float sponsorWidth = min(ImGui::GetContentRegionAvail().x,
						700.0F * settingGlobalScale);
					const float sponsorAspect = settingSign[9].width > 0
						? static_cast<float>(settingSign[9].height)
							/ static_cast<float>(settingSign[9].width)
						: 0.56F;
					const bool sponsorCardVisible = Design::BeginCard("##sponsor-card",
						{ sponsorWidth, 0.0F }, ImFluentCardStyle_Filled);
					if (sponsorCardVisible)
					{
						const float imageWidth = ImGui::GetContentRegionAvail().x;
						ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[9],
							{ imageWidth, imageWidth * sponsorAspect });
						Design::Text("成功赞助后，可以联系作者将您的昵称和赞助的金额添加到社区名片中以表示感谢。", ImFluentTextStyle_Body, Design::TextSecondary);
					}
					Design::EndCard();
					break;
				}
				case settingTabEnum::tab9:
				{
					Design::PageHeader(IA(I18nKey.SettingsUI.DebugSoftware.N).c_str());
					Design::SectionHeader("控制台输出");
					const auto consoleToggle = [&](const char* id, const char* title, const char* description,
						bool& value, auto& saved)
					{
						if (Design::ToggleRow(id, title, description, "\ue756", value))
						{
							saved = value;
							QueueConfigWrite();
						}
					};
					consoleToggle("##console-touch-area", IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.TouchArea.N).c_str(),
						IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.TouchArea.E).c_str(),
						Experimental.Inkeys3.TouchAreaConsoleOutput, Inkeys::config.Experimental.Inkeys3.ConsoleOutput.TouchArea);
					consoleToggle("##console-ppt-com", IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.PptCOM.N).c_str(),
						IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.PptCOM.E).c_str(),
						Experimental.Inkeys3.PptCOMConsoleOutput, Inkeys::config.Experimental.Inkeys3.ConsoleOutput.PptCOM);
					consoleToggle("##console-draw3", IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.Draw3.N).c_str(),
						IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.Draw3.E).c_str(),
						Experimental.Inkeys3.Draw3ConsoleOutput, Inkeys::config.Experimental.Inkeys3.ConsoleOutput.Draw3);
					consoleToggle("##console-cursor", IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.Cursor.N).c_str(),
						IA(I18nKey.SettingsUI.Experimental.ConsoleOutput.Cursor.E).c_str(),
						Experimental.Inkeys3.CursorConsoleOutput, Inkeys::config.Experimental.Inkeys3.ConsoleOutput.Cursor);
#if defined(DRAW3_RTS_DIAGNOSTICS)
					const auto diagnosticText = [](const char* format, auto... values)
					{
						char text[2048]{};
						snprintf(text, sizeof(text), format, values...);
						Design::Text(text);
					};
					const auto diagnosticSurfaceSize = [](float* size)
					{
						bool changed = false;
						Design::SettingRow("##diagnostic-surface-size", "当前方向表面宽高 (cm)", nullptr,
							"\ue9a6", 240.0F, 32.0F, [&](const Inkeys::UI::Setting::LayoutRect& bounds)
							{
								const float width = (bounds.Width() - Design::Pixels(8.0F)) * 0.5F;
								for (int axis = 0; axis < 2; ++axis)
								{
									ImGui::PushID(axis);
									ImGui::SetCursorScreenPos({ bounds.left + axis * (width + Design::Pixels(8.0F)), bounds.top });
									ImGui::SetNextItemWidth(width);
									double value = size[axis];
									if (Design::NumberBox("##axis", &value, 1.0, 10.0, "%.1f"))
									{
										size[axis] = static_cast<float>(value);
										changed = true;
									}
									ImGui::PopID();
								}
							});
						return changed;
					};
#endif
					const auto displaySnapshot = Inkeys::Display::GetSnapshot();
#if defined(DRAW3_RTS_DIAGNOSTICS)
					{
						using namespace Inkeys::Drawing::Draw3;
						auto options=ProductHost().EraserDevelopmentOptions();
						const auto scale=ProductHost().EraserDisplayScaleSnapshot();
						bool changed=false;
						Design::Text("笔速橡皮策略比较（临时，不保存）");
						int response=static_cast<int>(options.response);
						const char* models[]={"自动","强制间接 DIP","强制屏幕笔混合","强制直接 Touch 响应"};
						if(Design::ComboBox("响应模型",&response,models,4))
						{options.response=static_cast<SpeedEraser::ResponseOverride>(response);changed=true;}
						int source=static_cast<int>(options.scale);
						const char* scales[]={"自动","手动 Surface 28x18 cm（可修改）","强制物理不可用，测试分辨率-DPI回退"};
						if(Design::ComboBox("标尺来源",&source,scales,3))
						{
							options.scale=static_cast<SpeedEraser::ScaleOverride>(source);changed=true;
							if(options.scale==SpeedEraser::ScaleOverride::ManualSurface)
							{
								options.calibration.monitor=scale.monitor;
								options.calibration.orientation=scale.orientation;
								options.calibration.widthCm=(scale.orientation&1u)?18.0f:28.0f;
								options.calibration.heightCm=(scale.orientation&1u)?28.0f:18.0f;
							}
						}
						if(options.scale==SpeedEraser::ScaleOverride::ManualSurface)
						{
							const bool rotated=((options.calibration.orientation^scale.orientation)&1u)!=0;
							float size[]={rotated?options.calibration.heightCm:options.calibration.widthCm,
								rotated?options.calibration.widthCm:options.calibration.heightCm};
							if(diagnosticSurfaceSize(size))
							{
								options.calibration.widthCm=size[0];options.calibration.heightCm=size[1];
								options.calibration.monitor=scale.monitor;options.calibration.orientation=scale.orientation;
								changed=true;
							}
							diagnosticText("绑定绘制表面 %p；换屏不套用此尺寸",reinterpret_cast<void*>(options.calibration.monitor));
						}
						int beta=options.penBeta<0.375f?0:options.penBeta>0.625f?2:1;
						const char* betas[]={"0.25","0.50（默认）","0.75"};
						if(Design::ComboBox("屏幕笔 beta",&beta,betas,3))
						{options.penBeta=0.25f*(beta+1);changed=true;}
						if(Design::Checkbox("触摸接触面积辅助（实验）",&options.touchContactAreaAssistance))
						{
							changed=true;
							// 两个入口共用持久化状态，不保存其他临时调测参数。
							Inkeys::config.Experimental.Inkeys3.Draw3.TouchContactAreaAssistance =
								options.touchContactAreaAssistance;
							QueueConfigWrite();
						}
						diagnosticText("默认关闭。参考接触范围适度提高拖擦下限；驱动报告不准时请关闭。点按仍小尺寸起步，开启后精细拖擦下限可能增大。");
						changed|=Design::Checkbox("显示低成本笔速诊断",&options.diagnostics);
						if(changed)ProductHost().SetEraserDevelopmentOptions(options);
						diagnosticText("模型和标尺在下一独立接触/批次应用；真实 Pen/Touch 身份不变。经验尺度不是实测毫米。");
						if(options.diagnostics)
						{
							const auto d=ProductRuntimeSnapshot().eraser;
							const char* recognition=d.inputSource.recognition==SpeedEraser::SourceRecognition::PointerCursor?"Pointer cursor":
								d.inputSource.recognition==SpeedEraser::SourceRecognition::RtsCapabilities?"RTS 能力":
								d.inputSource.recognition==SpeedEraser::SourceRecognition::Conflict?"识别冲突":"未知";
							diagnosticText("真实输入 %u | %s / %s | 模型 %s | 标尺 %s",
								d.inputType,SpeedEraser::SourceKindName(d.inputSource.kind),recognition,
								SpeedEraser::ResponseModelName(d.response),SpeedEraser::ScaleSourceName(d.motionSource));
							diagnosticText("表面 %p | DPI %.0fx%.0f | 映射 %s | generation %llu / %llu",
								reinterpret_cast<void*>(d.monitor),d.dpiX,d.dpiY,d.inputMapped?"可靠":"未确认",
								static_cast<unsigned long long>(d.displayGeneration),static_cast<unsigned long long>(d.displayRevision));
							diagnosticText("实际像素 %dx%d | DIP/px %.5fx%.5f | 动作单位/px %.5fx%.5f",
								d.pixelWidth,d.pixelHeight,d.dipPerPixelX,d.dipPerPixelY,d.motionPerPixelX,d.motionPerPixelY);
							if(d.motionSource==SpeedEraser::ScaleSource::ManualCalibration)
								diagnosticText("本批手动表面 %.1fx%.1f cm（录入方向）",d.manualWidthCm,d.manualHeightCm);
							diagnosticText("速度 %.1f %s | 资格 %s | 证据 %.0f ms",
								d.speed,SpeedEraser::MotionUnitName(d.motionUnit),d.qualified?"有":"无",d.evidenceSeconds*1000);
							diagnosticText("有效直径 %.2f DIP | 最终光标 %.2f px | 下一段半径 %.2f px | 限幅 %s",
								d.effectiveDiameterDip,d.cursorDiameterPx,d.nextRadiusPx,d.limited?"是":"否");
							const auto& area=d.contactArea;
							diagnosticText("目标 %.2f DIP | Touch起步解锁 %s | 面积辅助 %s",
								d.targetDiameterDip,d.touchUnlocked?"是":"否",area.enabled?"开":"关");
							diagnosticText("接触 raw %.1fx%.1f | per-context %.2fx%.2f [%s]",
								area.sample.rawWidth,area.sample.rawHeight,area.sample.widthPx,area.sample.heightPx,
								SpeedEraser::ContactAreaUnitsName(area.sample.units));
							if(area.sample.units==SpeedEraser::ContactAreaUnits::CanvasPixels)
								diagnosticText("接触 DIP %.2fx%.2f | 参考新鲜 %s",area.widthDip,area.heightDip,area.referenceFresh?"是":"否");
							else Design::Text("接触 DIP 未确认，不用于面积辅助");
							diagnosticText("面积 %s | 有效样本 %s | 稳定拖擦 %.0f ms | 参考/有效下限 %.2f/%.2f DIP | 激活 %s",
								SpeedEraser::ContactAreaReasonName(area.reason),area.sampleValid?"是":"否",
								area.stableMotionSeconds*1000,area.referenceFloorDip,area.activeFloorDip,area.active?"是":"否");
							if(d.motionSource==SpeedEraser::ScaleSource::TrustedPhysical || d.motionSource==SpeedEraser::ScaleSource::ManualCalibration)
								diagnosticText("物理标尺 %.4f mm/DIP | beta %.2f",d.rhoMmPerDip,d.penBeta);
							else if(d.motionSource==SpeedEraser::ScaleSource::ResolutionDpiHeuristic)
								diagnosticText("经验尺度增益 %.3f（无量纲）",d.heuristicGain);
						}
						ImGui::Separator();ImGui::Spacing();
					}
#endif
					Design::SectionHeader("显示器与 EDID");
					std::string edidText;
					if (!displaySnapshot)
					{
						edidText = "显示快照不可用";
					}
					else
					{
						std::wstring summary = L"活动拓扑：";
						summary += Inkeys::Display::DisplayTopologyText(displaySnapshot->topology);
						summary += L" | 监视器数量：" + std::to_wstring(displaySnapshot->monitors.size());
						edidText = utf16ToUtf8(summary);

						for (std::size_t monitorIndex = 0; monitorIndex < displaySnapshot->monitors.size(); ++monitorIndex)
						{
							const auto& monitor = displaySnapshot->monitors[monitorIndex];
							const Inkeys::Display::ActiveDisplayTargetInfo* target = nullptr;
							if (monitor.targetIndex && *monitor.targetIndex < displaySnapshot->activeTargets.size())
								target = &displaySnapshot->activeTargets[*monitor.targetIndex];

							std::wstring monitorName = target && !target->monitorFriendlyName.empty()
								? target->monitorFriendlyName : L"未知显示器";
							std::wstring windowsMonitor = monitor.deviceName;
							const auto displayNamePosition = windowsMonitor.find(L"DISPLAY");
							if (displayNamePosition != std::wstring::npos)
								windowsMonitor.erase(0, displayNamePosition);

							std::wstring line = L"显示器 " + std::to_wstring(monitorIndex + 1);
							if (monitor.primary) line += L"（主）";
							line += L"：" + monitorName;
							line += L" | Windows " + windowsMonitor;
							line += L" | " + std::to_wstring(monitor.pixelWidth) + L"x" +
								std::to_wstring(monitor.pixelHeight) + L" px";
							line += L" | DPI " + std::to_wstring(monitor.effectiveDpiX) + L"x" +
								std::to_wstring(monitor.effectiveDpiY);
							if (monitor.physicalSize.available)
							{
								line += L" | 物理 " + std::to_wstring(monitor.physicalSize.widthCm) + L"x" +
									std::to_wstring(monitor.physicalSize.heightCm) + L" cm";
							}
							else
							{
								line += L" | 物理不可用（" + std::wstring(Inkeys::Display::PhysicalSizeUnavailableReasonText(
									monitor.physicalSize.unavailableReason)) + L"）";
							}
							line += L" | EDID " + std::wstring(Inkeys::Display::EdidStatusText(monitor.edid.status));
							line += L"，原始 " + std::to_wstring(monitor.edid.rawPhysicalWidthCm) + L"x" +
								std::to_wstring(monitor.edid.rawPhysicalHeightCm) + L" cm";

							edidText += "\n\n" + utf16ToUtf8(line);
						}
					}
					Design::RenderEdidCard(edidText.c_str());

					break;
				}
				}
				ImGui::EndChild();
				ImFluent::PopFont();
				Design::BindTextFonts(ImFontMain, ImFontStrong);
				Design::ApplyContentAlpha(contentWindow, Experimental.Inkeys3.AnimationEnable ? contentAlpha : 1.0F);
				ImGui::EndChild();
				ImGui::PopID();
				ImGui::PopID();
				ImGui::EndDisabled();
				ImGui::PopStyleVar();

				if (navigationState.overlayOpen)
				{
					ImGui::SetCursorPos({ Widgets::Dip(contentGeometry.left), Widgets::Dip(contentGeometry.top) });
					ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 8));
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
					ImGui::BeginChild("##settings-nav-scrim", { Widgets::Dip(contentGeometry.Width()), Widgets::Dip(contentGeometry.Height()) },
						ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
					if (ImGui::InvisibleButton("##dismiss-overlay", ImGui::GetContentRegionAvail()))
						navigationState.DismissOverlay();
					ImGui::EndChild();
					ImGui::PopStyleVar();
					ImGui::PopStyleColor();
				}
				Inkeys::UI::Setting::Design::BindTextFonts(ImFontDesignMain, ImFontDesignStrong);
				ImFluent::PushFont(ImFluentTextStyle_Body);
				synchronizeDestination();
				// 正文只渲染一次；若本帧关闭动画，导航使用已归位状态重新解析自己的几何。
				const auto navigationGeometry = Design::ResolveShellGeometry(static_cast<float>(SettingWindowWidth) / settingGlobalScale,
					static_cast<float>(SettingWindowHeight) / settingGlobalScale, navigationState);
				const auto navigationAction = Design::RenderNavigationPane(navigationGeometry, navigationState, destination);
				ImFluent::PopFont();
				Inkeys::UI::Setting::Design::BindTextFonts(ImFontMain, ImFontStrong);
				if (navigationAction.kind == Design::NavigationActionKind::Navigate)
				{
					navigateTo(navigationAction.destination);
					if (settingTab == settingTabEnum::tab4) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
				}
				else if (navigationAction.kind == Design::NavigationActionKind::Community)
					ShellExecuteW(0, 0, I18n::isIdentifying(L"zh-CN") ? L"https://www.inkeys.top/community.html" : L"https://en.inkeys.top/community.html", 0, 0, SW_SHOW);
				else if (navigationAction.kind == Design::NavigationActionKind::Restart)
				{ commitGeneralSliders(); Inkeys::UI::Setting::Hide(); RestartProgram(); }
				else if (navigationAction.kind == Design::NavigationActionKind::Exit)
				{ commitGeneralSliders(); Inkeys::UI::Setting::Hide(); CloseProgram(); }

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
		settingNavigationClockIdle = true;
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
		auto pauseNavigationClock = [](FrameResult result)
		{
			// 所有未实际绘制的路径都在渲染线程标记；可见低帧率仍持续推进动画。
			settingNavigationClockIdle = true;
			return result;
		};
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
			if (!succeeded) return pauseNavigationClock(FrameResult::Retry);
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
				return pauseNavigationClock(FrameResult::Retry);
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
				return pauseNavigationClock(FrameResult::Retry);
			Widgets::style.ApplyGlobal(Inkeys::UI::Setting::Design::ScrollbarTrackSize);
			Inkeys::UI::Setting::Design::ApplyScrollbars();
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ConsumeFontRebuild(fontRebuildSerial);
		}

		const uint64_t themeSerial = settingThemeSerial.load(memory_order_acquire);
		if (!settingSession.Done() && themeSerial != settingConsumedThemeSerial)
		{
			// 主题消息在隐藏态也立即更新 resident style，不等待下一次 Show。
			lock_guard imguiLock(settingImguiMutex);
			ApplySettingTheme(setting_window);
			settingConsumedThemeSerial = themeSerial;
		}

		// 只有 Move 可复用 compositor 缓存；Size 必须继续 resize/render/present。
		if (settingInteractiveOperation.load(memory_order_acquire)
			== Inkeys::UI::Setting::InteractiveWindowOperation::Move)
			return pauseNavigationClock(FrameResult::Idle);

		if (decision.createPresentation && !g_pSwapChain)
		{
			if (!CreatePresentation(setting_window, context.epoch))
				return pauseNavigationClock(FrameResult::Retry);
		}

		if (!decision.render || settingSession.Done() || !g_pSwapChain)
			return pauseNavigationClock(FrameResult::Idle);

		if (decision.probeOcclusion && g_pSwapChain)
		{
			const HRESULT probeResult = g_pSwapChain->Present(0, DXGI_PRESENT_TEST);
			if (probeResult == DXGI_STATUS_OCCLUDED) return pauseNavigationClock(FrameResult::Retry);
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(probeResult))
				return pauseNavigationClock(FrameResult::DeviceLost);
			if (FAILED(probeResult)) return pauseNavigationClock(FrameResult::Retry);
			g_SwapChainOccluded = false;
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetOccluded(false);
		}

		if (decision.resize && !settingSession.Done() && g_pSwapChain)
		{
			const HRESULT resizeResult = ResizeSwapChain(resize.width, resize.height);
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(resizeResult))
				return pauseNavigationClock(FrameResult::DeviceLost);
			if (FAILED(resizeResult))
			{
				CleanupPresentation();
				lock_guard stateLock(settingStateMutex);
				settingSessionState.ReleasePresentation();
				return pauseNavigationClock(FrameResult::Retry);
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
		if (settingFrameResult != FrameResult::Continue) settingNavigationClockIdle = true;
		return settingFrameResult;
	}
}

namespace Inkeys::UI::Setting
{
	void RequestConfigWrite() { QueueConfigWrite(); }

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
		const bool visible = IsVisible();
		bool focused = false;
		if (visible)
		{
			auto& windowService = Inkeys::Window::GetService();
			const HWND setting = windowService.Handle(
				Inkeys::Window::WindowRole::Setting);
			// Bar 不激活自身，因此以前台线程的真实焦点 HWND 判断 Setting 是否仍有焦点。
			focused = setting && IsWindow(setting)
				&& GetForegroundWindow() == setting
				&& Inkeys::Window::Service::LastFocusWindow() == setting;
		}

		switch (ResolveBarButtonClickAction(visible, focused))
		{
		case BarButtonClickAction::Hide:
			Hide();
			break;
		case BarButtonClickAction::ShowAndActivate:
		case BarButtonClickAction::Activate:
			// Show 命令会在 Setting owner thread 中恢复窗口并交还前台与输入焦点。
			Show();
			break;
		}
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
