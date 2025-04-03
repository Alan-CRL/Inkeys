#include "IdtFreezeFrame.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "IdtSetting.h"
#include "IdtText.h"
#include "IdtWindow.h"

int FreezeRecall;

void FreezeFrameWindow()
{
	threadStatus[L"FreezeFrameWindow"] = true;

	DisableResizing(freeze_window, true);//禁止窗口拉伸
	SetWindowLong(freeze_window, GWL_STYLE, GetWindowLong(freeze_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
	SetWindowPos(freeze_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
	SetWindowLong(freeze_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

	IMAGE freeze_background, PptSign;
	if (setlist.avoidFullScreen)
	{
		freeze_background.Resize(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight - 1);
		SetWindowPos(freeze_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight - 1, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	else
	{
		freeze_background.Resize(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight);
		SetWindowPos(freeze_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);
	idtLoadImage(&PptSign, L"PNG", L"sign4");

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { freeze_background.getwidth(),freeze_background.getheight() };
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

	while (!(GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(freeze_window, GWL_EXSTYLE, GetWindowLong(freeze_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(freeze_window, GWL_EXSTYLE, GetWindowLong(freeze_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	// TODO 临时方案
	::SetWindowLong(freeze_window, GWL_EXSTYLE, ::GetWindowLong(freeze_window, GWL_EXSTYLE) | WS_EX_TRANSPARENT);

	ulwi.hdcSrc = GetImageHDC(&freeze_background);
	UpdateLayeredWindowIndirect(freeze_window, &ulwi);

	IdtWindowsIsVisible.freezeWindow = true;
	//ShowWindow(freeze_window, SW_SHOW);

	FreezeFrame.update = true;
	int wait = 0;
	bool show_freeze_window = false;

	RECT fwords_rect;
	while (!offSignal)
	{
		this_thread::sleep_for(chrono::milliseconds(20));

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
					if (FreezeFrame.mode != 1 || ppt_show != NULL) break;

					if (FreezeRecall > 0)
					{
						SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);

						hiex::EasyX_Gdiplus_FillRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 160, (float)GetSystemMetrics(SM_CYSCREEN) - 200, 320, 50, 20, 20, RGBA(255, 255, 225, min(255, FreezeRecall)), RGBA(0, 0, 0, min(150, FreezeRecall)), 2, true, SmoothingModeHighQuality, &freeze_background);

						wchar_t buffer[100];
						if (RecallImageTm.tm_mday == 0) swprintf_s(buffer, L"超级恢复");
						else swprintf_s(buffer, L"超级恢复 %02d月%02d日 %02d:%02d:%02d", RecallImageTm.tm_mon + 1, RecallImageTm.tm_mday, RecallImageTm.tm_hour, RecallImageTm.tm_min, RecallImageTm.tm_sec);

						Graphics graphics(GetImageHDC(&freeze_background));
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 22, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(255, 255, 255, min(255, FreezeRecall)), true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							fwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 160;
							fwords_rect.top = GetSystemMetrics(SM_CYSCREEN) - 200;
							fwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 160;
							fwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) - 200 + 52;
						}
						graphics.DrawString(buffer, -1, &gp_font, hiex::RECTToRectF(fwords_rect), &stringFormat, &WordBrush);

						ulwi.hdcSrc = GetImageHDC(&freeze_background);
						UpdateLayeredWindowIndirect(freeze_window, &ulwi);

						FreezeRecall -= 10;

						if (FreezeRecall <= 0)
						{
							SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);
							ulwi.hdcSrc = GetImageHDC(&freeze_background);
							UpdateLayeredWindowIndirect(freeze_window, &ulwi);

							if (FreezeRecall <= 0) FreezeRecall = 0;
							break;
						}
					}

					this_thread::sleep_for(chrono::milliseconds(20));
				}

				if (ppt_show != NULL) FreezeFrame.mode = 0;
				FreezeFrame.update = true;
			}
			else if (show_freeze_window)
			{
				SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);
				ulwi.hdcSrc = GetImageHDC(&freeze_background);
				UpdateLayeredWindowIndirect(freeze_window, &ulwi);

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
				SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);

				ulwi.hdcSrc = GetImageHDC(&freeze_background);
				UpdateLayeredWindowIndirect(freeze_window, &ulwi);
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

				SetImageColor(freeze_background, RGBA(0, 0, 0, 140), true);
				hiex::TransparentImage(&freeze_background, GetSystemMetrics(SM_CXSCREEN) / 2 - 500, GetSystemMetrics(SM_CYSCREEN) / 2 - 150, &PptSign);

				hiex::EasyX_Gdiplus_SolidRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 300, (float)GetSystemMetrics(SM_CYSCREEN) / 2 + 200, 600, 10, 10, 10, RGBA(255, 255, 255, 100), true, SmoothingModeHighQuality, &freeze_background);
				hiex::EasyX_Gdiplus_SolidRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 300, (float)GetSystemMetrics(SM_CYSCREEN) / 2 + 200, (float)max(0, min(50, wy)) * 12, 10, 10, 10, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &freeze_background);

				{
					Graphics graphics(GetImageHDC(&freeze_background));
					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(255, 255, 255, 255), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 500;
						dwords_rect.top = GetSystemMetrics(SM_CYSCREEN) / 2 + 250;
						dwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 500;
						dwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) / 2 + 300;
					}
					graphics.DrawString(L"Tips：无需处于选择模式，点击下方按钮即可翻页", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
				}

				ulwi.hdcSrc = GetImageHDC(&freeze_background);
				UpdateLayeredWindowIndirect(freeze_window, &ulwi);

				//SetForegroundWindow(ppt_show);

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
				SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);

				hiex::EasyX_Gdiplus_FillRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 160, (float)GetSystemMetrics(SM_CYSCREEN) - 200, 320, 50, 20, 20, RGBA(255, 255, 225, min(255, FreezeRecall)), RGBA(0, 0, 0, min(150, FreezeRecall)), 2, true, SmoothingModeHighQuality, &freeze_background);

				wchar_t buffer[100];
				if (RecallImageTm.tm_mday == 0) swprintf_s(buffer, L"超级恢复");
				else swprintf_s(buffer, L"超级恢复 %02d月%02d日 %02d:%02d:%02d", RecallImageTm.tm_mon + 1, RecallImageTm.tm_mday, RecallImageTm.tm_hour, RecallImageTm.tm_min, RecallImageTm.tm_sec);

				Graphics graphics(GetImageHDC(&freeze_background));
				Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 22, FontStyleRegular, UnitPixel);
				SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(255, 255, 255, min(255, FreezeRecall)), true));
				graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
				{
					fwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 160;
					fwords_rect.top = GetSystemMetrics(SM_CYSCREEN) - 200;
					fwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 160;
					fwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) - 200 + 52;
				}
				graphics.DrawString(buffer, -1, &gp_font, hiex::RECTToRectF(fwords_rect), &stringFormat, &WordBrush);

				ulwi.hdcSrc = GetImageHDC(&freeze_background);
				UpdateLayeredWindowIndirect(freeze_window, &ulwi);

				FreezeRecall -= 10;
				this_thread::sleep_for(chrono::milliseconds(20));

				if (FreezeRecall <= 0)
				{
					SetImageColor(freeze_background, RGBA(0, 0, 0, 0), true);

					ulwi.hdcSrc = GetImageHDC(&freeze_background);
					UpdateLayeredWindowIndirect(freeze_window, &ulwi);

					if (FreezeRecall <= 0) FreezeRecall = 0;
					break;
				}
			}
		}
	}
	threadStatus[L"FreezeFrameWindow"] = false;
}