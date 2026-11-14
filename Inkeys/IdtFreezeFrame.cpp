import Inkeys.Helper.Thread;
import Inkeys.Text.Font;
import Inkeys.Window;
import Inkeys.Startup.Progress;
import Inkeys.UI.Freeze;

#include "IdtFreezeFrame.h"

#include "IdtConfiguration.h"
import Inkeys.Display;
#include "IdtDraw.h"
#include "IdtI18n.h"
#include "IdtImage.h"
#include "IdtPlug-in.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

int FreezeRecall;

namespace
{
	std::mutex freezeSurfaceMutex;
	std::atomic_bool whiteboardFreezeSurfaceOwned = false;


	class FreezeSurfaceResources
	{
	public:
		Inkeys::Graphics::DibSurface frame, pptSign;

		[[nodiscard]] bool Ensure(int width, int height) noexcept
		{
			return frame.resize(width, height);
		}

		void Release() noexcept
		{
			frame.reset();
			pptSign.reset();
		}

		[[nodiscard]] std::uint64_t PayloadBytes() const noexcept
		{
			return static_cast<std::uint64_t>(frame.width()) * frame.height() * 4
				+ static_cast<std::uint64_t>(pptSign.width()) * pptSign.height() * 4;
		}

		template<class Submit>
		[[nodiscard]] bool RetireIfUnused(bool required, bool whiteboardOwner, Submit&& submit)
		{
			if (whiteboardOwner)
			{
				// 共享 HWND 已交给白板；只归还自有像素，不能用透明帧覆盖新 owner。
				Release();
				return true;
			}
			if (required) return false;
			if (!frame.empty())
			{
				frame.clear();
				if (!submit(frame)) return false;
			}
			// 每次退休依赖这次真实透明提交，不沿用跨窗口/所有权的旧证明。
			Release();
			return true;
		}
	};
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

	FreezeSurfaceResources surfaces;
	auto& freeze_background = surfaces.frame;
	auto& PptSign = surfaces.pptSign;
	const auto displaySnapshot = Inkeys::Display::GetSnapshot();
	const auto* monitor = displaySnapshot ? displaySnapshot->Primary() : nullptr;
	if (!monitor)
	{
		(void)Inkeys::Startup::ReportFailure(0xF002u);
		return;
	}
	const int monitorHeight = monitor->pixelHeight;
	// 固定保留 1 像素，避免被系统判定为全屏；旧 AvoidFullScreen 配置已废弃。
	const int freezeHeight = monitorHeight - 1;
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


	const auto submitSurface = [&](const Inkeys::Graphics::DibSurface& surface)
	{
		sizeWnd = { surface.width(), surface.height() };
		ulwi.hdcSrc = surface.dc();
		return SubmitFreezeSurface(freeze_window, &ulwi, false);
	};

	ulwi.hdcSrc = freeze_background.dc();
	if (submitSurface(freeze_background))
	{
		(void)Inkeys::Startup::Report(
			Inkeys::Startup::Milestone::FreezeFirstFrameCommitted);
		IdtWindowsIsVisible.freezeWindow = true;
		// 启动首帧已经真实提交；普通静止不再持有一整屏 DIB。
		surfaces.Release();
		ulwi.hdcSrc = nullptr;
	}
	else
		(void)Inkeys::Startup::ReportFailure(0xF001u);

	//ShowWindow(freeze_window, SW_SHOW);

	int wait = 0;
	bool show_freeze_window = false;

	RECT fwords_rect;
	while (!offSignal)
	{
		this_thread::sleep_for(chrono::milliseconds(20));
		const bool whiteboardOwner = WhiteboardFreezeSurfaceOwned();
		const bool freezeActive = Inkeys::UI::Freeze::IsActive();
		const bool pixelsRequired = freezeActive || FreezeRecall > 0 || FreezePPT;
		if (whiteboardOwner || !pixelsRequired)
		{
			if (surfaces.RetireIfUnused(pixelsRequired, whiteboardOwner, submitSurface))
			{
				ulwi.hdcSrc = nullptr;
				show_freeze_window = false;
			}
			// 同次状态判定后直接退出，不能让并发的新提示落入已经回收的 surface。
			continue;
		}
		const bool recreated = freeze_background.empty();
		if (!surfaces.Ensure(monitor->pixelWidth, freezeHeight)) continue;
		sizeWnd = { freeze_background.width(), freeze_background.height() };
		ulwi.hdcSrc = freeze_background.dc();
		if (recreated && freezeActive && FreezeRecall <= 0 && !FreezePPT)
		{
			// 白板可能曾写过同一 HWND；新 Freeze 会话必须先提交自己的透明画面。
			freeze_background.clear();
			if (!submitSurface(freeze_background)) continue;
		}

		if (Inkeys::UI::Freeze::IsActive())
		{
			show_freeze_window = true;
			while (!offSignal)
			{
				if (!Inkeys::UI::Freeze::IsActive() || WhiteboardFreezeSurfaceOwned()) break;

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
					(void)submitSurface(freeze_background);

					FreezeRecall -= 10;
					if (FreezeRecall <= 0)
					{
						freeze_background.clear();
						ulwi.hdcSrc = freeze_background.dc();
						(void)submitSurface(freeze_background);
						FreezeRecall = 0;
						break;
					}
				}
				this_thread::sleep_for(chrono::milliseconds(20));
			}
		}
		else if (show_freeze_window)
		{
			freeze_background.clear();
			ulwi.hdcSrc = freeze_background.dc();
			(void)submitSurface(freeze_background);
			show_freeze_window = false;
		}
		if (!Inkeys::UI::Freeze::IsActive() && FreezePPT)
		{
			if (PptSign.empty()) (void)LoadSurfaceFromResource(&PptSign, L"PNG", L"sign4");
			if (!show_freeze_window)
			{
				freeze_background.clear();

				ulwi.hdcSrc = freeze_background.dc();
				(void)submitSurface(freeze_background);
				show_freeze_window = true;
			}

			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();
			chrono::high_resolution_clock::time_point tRecord;
			for (; FreezePPT && !offSignal && !WhiteboardFreezeSurfaceOwned(); )
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
				(void)submitSurface(freeze_background);

				//FocusPptShow();

				{
					double delay = 1000.0 / 24.0 - chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - tRecord).count();
					if (delay >= 1.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
				}
			}
			FreezePPT = false;
		}
		if (!Inkeys::UI::Freeze::IsActive() && FreezeRecall)
		{
			while (!offSignal && !WhiteboardFreezeSurfaceOwned())
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
				(void)submitSurface(freeze_background);

				FreezeRecall -= 10;
				this_thread::sleep_for(chrono::milliseconds(20));

				if (FreezeRecall <= 0)
				{
					freeze_background.clear();

					ulwi.hdcSrc = freeze_background.dc();
					(void)submitSurface(freeze_background);

					if (FreezeRecall <= 0) FreezeRecall = 0;
					break;
				}
			}
		}
	}
	// 屏幕 DC 只由本线程持有，线程退出前配对释放。
	if (hdcScreen) ReleaseDC(nullptr, hdcScreen);
}


int RunFreezeSurfaceResourceLifecycleTest() noexcept
{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	int failures = 0;
	const auto check = [&](bool value, const char* message)
	{
		if (!value) { ++failures; std::fprintf(stderr, "[FreezeResources] FAIL %s\n", message); }
	};
	const HINSTANCE instance = GetModuleHandleW(nullptr);
	constexpr wchar_t className[] = L"InkeysFreezeResourceLifecycle";
	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = DefWindowProcW;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = className;
	const ATOM atom = RegisterClassW(&windowClass);
	if (!atom) return 2;
	const HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		className, L"", WS_POPUP, -32000, -32000, 64, 47, nullptr, nullptr, instance, nullptr);
	if (!hwnd) { UnregisterClassW(className, instance); return 2; }
	const bool previousOwner = WhiteboardFreezeSurfaceOwned();
	SetWhiteboardFreezeSurfaceOwned(false);
	const DWORD baselineGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	unsigned submissions = 0;
	bool failSubmission = false;
	const auto submit = [&](const Inkeys::Graphics::DibSurface& surface)
	{
		++submissions;
		POINT destination{ -32000, -32000 }, source{};
		SIZE size{ surface.width(), surface.height() };
		BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UPDATELAYEREDWINDOWINFO info{};
		info.cbSize = failSubmission ? 0 : sizeof(info);
		info.pptDst = &destination; info.pptSrc = &source; info.psize = &size;
		info.hdcSrc = surface.dc(); info.pblend = &blend; info.dwFlags = ULW_ALPHA;
		return SubmitFreezeSurface(hwnd, &info, false);
	};
	FreezeSurfaceResources resources;
	check(resources.PayloadBytes() == 0 && resources.frame.empty(), "cold owner has no pixel allocation");
	check(resources.Ensure(64, 47), "real DIB is prepared for the first frame");
	resources.frame.clear();
	check(submit(resources.frame), "real transparent initial ULW succeeds");
	resources.Release();
	check(resources.PayloadBytes() == 0 && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baselineGdi,
		"successful first frame can release its DIB and GDI objects");
	const unsigned initialSubmissions = submissions;
	for (unsigned repeat = 0; repeat < 8; ++repeat)
		check(resources.RetireIfUnused(false, false, submit), "empty idle owner stays retired");
	check(submissions == initialSubmissions, "idle does not allocate or submit again");
	check(resources.Ensure(64, 47) && resources.pptSign.resize(16, 16), "reentry creates real frame and sign surfaces");
	resources.frame.clear(0x80402010u);
	check(submit(resources.frame), "colored active frame is really submitted");
	const auto activeBytes = resources.PayloadBytes();
	check(!resources.RetireIfUnused(true, false, submit) && resources.PayloadBytes() == activeBytes,
		"active owner retains its pixel payload");
	failSubmission = true;
	check(!resources.RetireIfUnused(false, false, submit) && resources.PayloadBytes() == activeBytes,
		"failed real transparent ULW retains payload for retry");
	failSubmission = false;
	check(resources.RetireIfUnused(false, false, submit) && resources.PayloadBytes() == 0,
		"successful transparent retry releases frame and sign");
	check(resources.Ensure(80, 55), "another session obtains current dimensions");
	resources.frame.clear(0xFF102030u);
	check(submit(resources.frame), "second active frame is committed");
	SetWhiteboardFreezeSurfaceOwned(true);
	const unsigned beforeWhiteboardRetirement = submissions;
	check(resources.RetireIfUnused(true, true, submit) && resources.PayloadBytes() == 0
		&& submissions == beforeWhiteboardRetirement, "whiteboard handoff frees only our pixels without a transparent submission");
	SetWhiteboardFreezeSurfaceOwned(false);
	check(resources.Ensure(64, 47), "new legacy owner can reenter after whiteboard");
	resources.frame.clear();
	check(submit(resources.frame), "new owner submits a new transparent frame instead of reusing an old proof");
	resources.Release();
	check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baselineGdi, "all session DIB/DC objects return to baseline");
	SetWhiteboardFreezeSurfaceOwned(previousOwner);
	DestroyWindow(hwnd);
	UnregisterClassW(className, instance);
	std::fprintf(stderr, "[FreezeResources] failures=%d resource_bytes_active=%llu resource_bytes_idle=%llu submissions=%u\n",
		failures, static_cast<unsigned long long>(activeBytes),
		static_cast<unsigned long long>(resources.PayloadBytes()), submissions);
	return failures ? 1 : 0;
#else
	return 2;
#endif
}
