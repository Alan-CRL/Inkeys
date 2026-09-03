import Inkeys.Helper.Thread;
import Inkeys.Text.Font;
import Inkeys.Window;
import Inkeys.Startup.Progress;

#include "IdtFreezeFrame.h"

#include "IdtConfiguration.h"
import Inkeys.Display;
#include "IdtDraw.h"
#include "IdtI18n.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <atomic>
#include <mutex>

int FreezeRecall;

namespace
{
	std::mutex freezeSurfaceMutex;
	std::atomic_bool whiteboardFreezeSurfaceOwned = false;
}

void SetWhiteboardFreezeSurfaceOwned(bool owned) noexcept
{
	whiteboardFreezeSurfaceOwned.store(owned, std::memory_order_release);
}

bool WhiteboardFreezeSurfaceOwned() noexcept
{
	return whiteboardFreezeSurfaceOwned.load(std::memory_order_acquire);
}

bool SubmitFreezeSurface(HWND hwnd, UPDATELAYEREDWINDOWINFO* info,
	bool whiteboardOwner) noexcept
{
	if (!hwnd || !info || WhiteboardFreezeSurfaceOwned() != whiteboardOwner) return false;
	std::scoped_lock lock(freezeSurfaceMutex);
	if (WhiteboardFreezeSurfaceOwned() != whiteboardOwner) return false;
	return UpdateLayeredWindowIndirect(hwnd, info) != FALSE;
}

void FreezeFrameWindow()
{
	Inkeys::Thread::StatusGuard guard("FreezeFrameWindow");

	Inkeys::Graphics::DibSurface freeze_background, PptSign;
	const auto displaySnapshot = Inkeys::Display::GetSnapshot();
	const auto* monitor = displaySnapshot ? displaySnapshot->Primary() : nullptr;
	if (!monitor)
	{
		(void)Inkeys::Startup::ReportFailure(0xF002u);
		return;
	}
	const int monitorHeight = monitor->pixelHeight;
	const int freezeHeight = setlist.regularSetting.avoidFullScreen
		? monitorHeight - 1
		: monitorHeight;
	if (!freeze_background.resize(monitor->pixelWidth, freezeHeight))
	{
		if (IDTLogger) IDTLogger->error("[定格线程][FreezeFrameWindow] 创建 DIB Surface 失败");
		(void)Inkeys::Startup::ReportFailure(0xF003u);
		return;
	}
	RECT freezeBounds{ monitor->bounds.left, monitor->bounds.top,
		monitor->bounds.left + monitor->pixelWidth,
		monitor->bounds.top + freezeHeight };
	(void)Inkeys::Window::GetService().SetBounds(
		Inkeys::Window::WindowRole::Freeze, freezeBounds);
	freeze_background.clear();
	LoadSurfaceFromResource(&PptSign, L"PNG", L"sign4");

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { freeze_background.width(),freeze_background.height() };
	POINT ptDst = { 0,0 }; // 设置窗口位置
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	ulwi.cbSize = sizeof(ulwi);
	ulwi.hdcDst = hdcScreen;
	ulwi.pptDst = &ptDst;
	ulwi.psize = &sizeWnd;
	ulwi.pptSrc = &ptSrc;
	ulwi.crKey = RGB(255, 255, 255);
	ulwi.pblend = &blend;
	ulwi.dwFlags = ULW_ALPHA;

	ulwi.hdcSrc = freeze_background.dc();
	if (SubmitFreezeSurface(freeze_window, &ulwi, false))
	{
		(void)Inkeys::Startup::Report(
			Inkeys::Startup::Milestone::FreezeFirstFrameCommitted);
		IdtWindowsIsVisible.freezeWindow = true;
	}
	else
		(void)Inkeys::Startup::ReportFailure(0xF001u);

	//ShowWindow(freeze_window, SW_SHOW);

	FreezeFrame.update = true;
	int wait = 0;
	bool show_freeze_window = false;

	RECT fwords_rect;
	while (!offSignal)
	{
		this_thread::sleep_for(chrono::milliseconds(20));
		if (WhiteboardFreezeSurfaceOwned()) continue;

		if (magnificationReady)
		{
			if (FreezeFrame.mode == 1)
			{
				if (!show_freeze_window)
				{
					RequestUpdateMagWindow = 1;
					show_freeze_window = true;
				}

				while (!offSignal)
				{
					if (FreezeFrame.mode != 1 || PptInfoState.TotalPage != -1) break;

					if (FreezeRecall > 0)
					{
						freeze_background.clear();

						DrawFilledSurfaceRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 160, (float)GetSystemMetrics(SM_CYSCREEN) - 200, 320, 50, 20, 20, RGBA(255, 255, 225, min(255, FreezeRecall)), RGBA(0, 0, 0, min(150, FreezeRecall)), 2, true, SmoothingModeHighQuality, &freeze_background);

						wchar_t buffer[100];
						if (RecallImageTm.tm_mday == 0) swprintf_s(buffer, L"超级恢复");
						else swprintf_s(buffer, L"超级恢复 %02d月%02d日 %02d:%02d:%02d", RecallImageTm.tm_mon + 1, RecallImageTm.tm_mday, RecallImageTm.tm_hour, RecallImageTm.tm_min, RecallImageTm.tm_sec);

						Graphics graphics(freeze_background.dc());
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 22, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(ToGdiplusColor(RGBA(255, 255, 255, min(255, FreezeRecall)), true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							fwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 160;
							fwords_rect.top = GetSystemMetrics(SM_CYSCREEN) - 200;
							fwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 160;
							fwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) - 200 + 52;
						}
						graphics.DrawString(buffer, -1, &gp_font, ToGdiplusRect(fwords_rect), &stringFormat, &WordBrush);

						ulwi.hdcSrc = freeze_background.dc();
						(void)SubmitFreezeSurface(freeze_window, &ulwi, false);

						FreezeRecall -= 10;

						if (FreezeRecall <= 0)
						{
							freeze_background.clear();
							ulwi.hdcSrc = freeze_background.dc();
							(void)SubmitFreezeSurface(freeze_window, &ulwi, false);

							if (FreezeRecall <= 0) FreezeRecall = 0;
							break;
						}
					}

					this_thread::sleep_for(chrono::milliseconds(20));
				}

				if (PptInfoState.TotalPage != -1) FreezeFrame.mode = 0;
				FreezeFrame.update = true;
			}
			else if (show_freeze_window)
			{
			freeze_background.clear();
			ulwi.hdcSrc = freeze_background.dc();
				(void)SubmitFreezeSurface(freeze_window, &ulwi, false);

				RequestUpdateMagWindow = 0;
				show_freeze_window = false;
			}
		}
		else if (FreezeFrame.mode == 1)
		{
			FreezeFrame.mode = 0;
			FreezeFrame.select = false;
		}

		if (FreezeFrame.mode != 1 && FreezePPT)
		{
			if (!show_freeze_window)
			{
				freeze_background.clear();

				ulwi.hdcSrc = freeze_background.dc();
				(void)SubmitFreezeSurface(freeze_window, &ulwi, false);
				show_freeze_window = true;
			}

			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();
			chrono::high_resolution_clock::time_point tRecord;
			for (; FreezePPT && !offSignal; )
			{
				tRecord = chrono::high_resolution_clock::now();

				double cost = chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
				if (cost >= 3000.0)
				{
					ppt_title_recond[ppt_title] = true;
					break;
				}
				int wy = static_cast<int>(cost * 0.02333 - 10.0);

				freeze_background.clear(PackSurfaceBgra(RGBA(0, 0, 0, 140)));
				(void)freeze_background.composite(PptSign, GetSystemMetrics(SM_CXSCREEN) / 2 - 500, GetSystemMetrics(SM_CYSCREEN) / 2 - 150);

				FillSurfaceRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 300, (float)GetSystemMetrics(SM_CYSCREEN) / 2 + 200, 600, 10, 10, 10, RGBA(255, 255, 255, 100), true, SmoothingModeHighQuality, &freeze_background);
				FillSurfaceRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 300, (float)GetSystemMetrics(SM_CYSCREEN) / 2 + 200, (float)max(0, min(50, wy)) * 12, 10, 10, 10, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &freeze_background);

				{
					Graphics graphics(freeze_background.dc());
					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(ToGdiplusColor(RGBA(255, 255, 255, 255), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 500;
						dwords_rect.top = GetSystemMetrics(SM_CYSCREEN) / 2 + 250;
						dwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 500;
						dwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) / 2 + 300;
					}
					graphics.DrawString(L"Tips：无需处于选择模式，点击下方按钮即可翻页", -1, &gp_font, ToGdiplusRect(dwords_rect), &stringFormat, &WordBrush);
				}

				ulwi.hdcSrc = freeze_background.dc();
				(void)SubmitFreezeSurface(freeze_window, &ulwi, false);

				//FocusPptShow();

				{
					double delay = 1000.0 / 24.0 - chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - tRecord).count();
					if (delay >= 1.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
				}
			}
			FreezePPT = false;
		}
		if (FreezeFrame.mode != 1 && FreezeRecall)
		{
			while (!offSignal)
			{
				freeze_background.clear();

				DrawFilledSurfaceRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 160, (float)GetSystemMetrics(SM_CYSCREEN) - 200, 320, 50, 20, 20, RGBA(255, 255, 225, min(255, FreezeRecall)), RGBA(0, 0, 0, min(150, FreezeRecall)), 2, true, SmoothingModeHighQuality, &freeze_background);

				wchar_t buffer[100];
				if (RecallImageTm.tm_mday == 0) swprintf_s(buffer, IW("UI/Operate/Recall").c_str());
				else swprintf_s(buffer, (IW("UI/Operate/Recall") + L" %02d%02d %02d:%02d:%02d").c_str(), RecallImageTm.tm_mon + 1, RecallImageTm.tm_mday, RecallImageTm.tm_hour, RecallImageTm.tm_min, RecallImageTm.tm_sec);

				Graphics graphics(freeze_background.dc());
				Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 22, FontStyleRegular, UnitPixel);
				SolidBrush WordBrush(ToGdiplusColor(RGBA(255, 255, 255, min(255, FreezeRecall)), true));
				graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
				{
					fwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 160;
					fwords_rect.top = GetSystemMetrics(SM_CYSCREEN) - 200;
					fwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 160;
					fwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) - 200 + 52;
				}
				graphics.DrawString(buffer, -1, &gp_font, ToGdiplusRect(fwords_rect), &stringFormat, &WordBrush);

				ulwi.hdcSrc = freeze_background.dc();
				(void)SubmitFreezeSurface(freeze_window, &ulwi, false);

				FreezeRecall -= 10;
				this_thread::sleep_for(chrono::milliseconds(20));

				if (FreezeRecall <= 0)
				{
					freeze_background.clear();

					ulwi.hdcSrc = freeze_background.dc();
					(void)SubmitFreezeSurface(freeze_window, &ulwi, false);

					if (FreezeRecall <= 0) FreezeRecall = 0;
					break;
				}
			}
		}
	}
}
