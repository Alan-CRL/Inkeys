module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtD2DPreparation.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtState.h"
#include "../../../IdtWindow.h"

// ====================
// 临时
extern IdtAtomic<bool> ConfirmaNoMouMsgSignal, ConfirmaNoMouFunSignal;
void FloatingInstallHook();

module Inkeys.UI.Bar;
import :Main;
import :Atomic;
import :Zoom;

import <ranges>;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;

// ====================
// 窗口

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
	{
		if (setlist.regularSetting.clickRecover && ConfirmaNoMouMsgSignal)
			ConfirmaNoMouMsgSignal = false;
	}

	switch (msg)
	{
	case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
	{
		DWORD flags = 0;
		flags |= (0x00000001);
		flags |= (0x00000008);
		flags |= (0x00000100);
		flags |= (0x00000200);
		flags |= (0x00010000);
		return (LRESULT)flags;
	}

	case WM_TOUCH:
	{
		// 由于是专门使用 static 来存储当前窗口的触摸信息，所以该过程函数仅能给 barWindow 使用。

		static DWORD activeTouchId = 0;   // 0表示无活动ID
		static bool isTouchActive = false;
		static bool activeTouchIsPrimary = false;
		static short activeTouchX = 0;
		static short activeTouchY = 0;

		UINT cInputs = LOWORD(wParam);
		vector<TOUCHINPUT> inputs(cInputs);
		if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, inputs.data(), sizeof(TOUCHINPUT)))
		{
			POINT pt;
			bool hasPrimaryTouch = false;
			bool fallbackTouchLocked = false;

			for (UINT i = 0; i < cInputs; i++)
			{
				if (inputs[i].dwFlags & TOUCHEVENTF_PRIMARY)
				{
					hasPrimaryTouch = true;
					break;
				}
			}

			for (UINT i = 0; i < cInputs; i++)
			{
				const TOUCHINPUT& ti = inputs[i];
				bool isPrimaryTouch = (ti.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
				bool canLockFallbackTouch = !hasPrimaryTouch && !isTouchActive && !fallbackTouchLocked;

				double xO = static_cast<double>(ti.x) / 100.0;
				double yO = static_cast<double>(ti.y) / 100.0;

				pt.x = static_cast<LONG>(xO + 0.5);
				pt.y = static_cast<LONG>(yO + 0.5);
				ScreenToClient(hWnd, &pt);

				if ((ti.dwFlags & TOUCHEVENTF_DOWN) && (isPrimaryTouch || canLockFallbackTouch))
				{
					if (isTouchActive && activeTouchId != ti.dwID)
					{
						activeTouchId = 0;
						isTouchActive = false;
						activeTouchIsPrimary = false;
						Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONUP;
							msgMouse.x = activeTouchX;
							msgMouse.y = activeTouchY;
							msgMouse.lbutton = false;

							int index = hiex::GetWindowIndex(hWnd, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}

					// 如果当前无 activeID，则锁定 primary touch；没有 primary 标志时兜底第一个 DOWN 点
					if (!isTouchActive)
					{
						activeTouchId = ti.dwID;
						isTouchActive = true;
						activeTouchIsPrimary = isPrimaryTouch;
						fallbackTouchLocked = !isPrimaryTouch;
						Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, true);

						activeTouchX = pt.x;
						activeTouchY = pt.y;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONDOWN;
							msgMouse.x = pt.x;
							msgMouse.y = pt.y;
							msgMouse.lbutton = true;

							int index = hiex::GetWindowIndex(hWnd, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}
				bool canTranslateActiveTouch = isTouchActive && ti.dwID == activeTouchId && (isPrimaryTouch || !activeTouchIsPrimary || !hasPrimaryTouch);

				if ((ti.dwFlags & TOUCHEVENTF_MOVE) && canTranslateActiveTouch)
				{
					if (isTouchActive && ti.dwID == activeTouchId)
					{
						if (isPrimaryTouch) activeTouchIsPrimary = true;
						activeTouchX = pt.x;
						activeTouchY = pt.y;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_MOUSEMOVE;
							msgMouse.x = pt.x;
							msgMouse.y = pt.y;
							msgMouse.lbutton = true;

							int index = hiex::GetWindowIndex(hWnd, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}
				if ((ti.dwFlags & TOUCHEVENTF_UP) && canTranslateActiveTouch)
				{
					if (isTouchActive && ti.dwID == activeTouchId)
					{
						activeTouchId = 0;
						isTouchActive = false;
						activeTouchIsPrimary = false;
						Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);

						activeTouchX = pt.x;
						activeTouchY = pt.y;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONUP;
							msgMouse.x = pt.x;
							msgMouse.y = pt.y;
							msgMouse.lbutton = false;

							int index = hiex::GetWindowIndex(hWnd, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}

			}
		}

		CloseTouchInputHandle((HTOUCHINPUT)lParam);

		return 0;
	}

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MOUSEMOVE:
	{
		// 如果是触摸模拟出来的鼠标消息，就直接丢掉
		DWORD extraInfo = GetMessageExtraInfo();
		if ((extraInfo & 0xFFFFFF00) == 0xFF515700) return 0;
		if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, true);
		if (msg == WM_LBUTTONUP) Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);

		// 否则当成真正的鼠标消息处理

		break;
	}

	default:
		return HIWINDOW_DEFAULT_PROC;
	}

	return HIWINDOW_DEFAULT_PROC;
}

// ====================
// 媒体

// 媒体操控类
void BarMediaClass::LoadExImage() {}
void BarMediaClass::LoadFormat()
{
	formatCache = make_unique<BarFormatCache>(dWriteFactory1.Get());
}

// ====================
// 界面

void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
{
	// 1. 计算目标帧时间 (毫秒)
	// 例如: 60FPS -> 16.666... ms
	double targetFrameTimeMs = 1000.0 / targetFPS;

	// 2. 计算还需要等待的时间 (毫秒)
	double waitTimeMs = targetFrameTimeMs - frameTimeSpentMs;

	// 如果已经超时（掉帧），直接返回，不等待
	if (waitTimeMs <= 0.0)
	{
		return;
	}

	// 获取高精度计时器的频率 (Ticks Per Second)
	static LARGE_INTEGER freq = { 0 };
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

	// 记录开始等待时刻的 QPC
	LARGE_INTEGER startCounter, currentCounter;
	QueryPerformanceCounter(&startCounter);

	// 将等待时间 (ms) 转换为 QPC 的 Ticks 单位
	// 公式: (ms * freq) / 1000
	long long waitTicks = (long long)((waitTimeMs * (double)freq.QuadPart) / 1000.0);
	long long targetEndTick = startCounter.QuadPart + waitTicks;

	// === 阶段一：Sleep (粗略等待) ===
	// 只有当剩余时间大于 2ms 时才启用 Sleep，留出 1.5ms 的安全余量给 Spin
	if (waitTimeMs > 2.0)
	{
		// 预留约 1.5ms 的时间给最后的忙等待，其余时间睡觉
		// 注意这里显式使用 std::milli
		double sleepMs = waitTimeMs - 1.5;
		std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepMs));
	}

	// === 阶段二：Spin (高精度忙等待) ===
	// 死循环直到 QPC 达到目标 Tick
	do
	{
		QueryPerformanceCounter(&currentCounter);

		YieldProcessor();
	} while (currentCounter.QuadPart < targetEndTick);
}

// 具体渲染
BarUIRendering::BarUIRendering(BarUISetClass* barUISetClassT) { barUISetClass = barUISetClassT; }

bool BarUIRendering::Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (shape.enable.val == false) return false;
	if (!shape.fill.has_value() && !shape.frame.has_value()) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (shape.w.val <= 0 || shape.h.val <= 0) return false;
	if (shape.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = shape.w.val;
	double tarH = shape.h.val;
	double tarPct = shape.pct.val; // 透明度

	double tarRw = 0.0;
	double tarRh = 0.0;
	if (shape.rw.has_value()) tarRw = shape.rw.value().val;
	if (shape.rh.has_value()) tarRh = shape.rh.value().val;

	FLOAT tarZoom = static_cast<FLOAT>(barUISetClass->barStyle.zoom);
	D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<FLOAT>(tarX) * tarZoom, static_cast<FLOAT>(tarY) * tarZoom, static_cast<FLOAT>(tarX + tarW) * tarZoom, static_cast<FLOAT>(tarY + tarH) * tarZoom), static_cast<FLOAT>(tarRw) * tarZoom, static_cast<FLOAT>(tarRh) * tarZoom);

	// Clip
	if (clip)
	{
		ComPtr<ID2D1SolidColorBrush> spFillBrush;
		deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(RGB(0, 0, 0), 0.0), &spFillBrush);

		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillRoundedRectangle(&roundedRect, spFillBrush.Get());
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}
	// 渲染到 DC
	{
		// 渲染填充
		if (shape.fill.has_value())
		{
			COLORREF fill = shape.fill.value().val;

			ComPtr<ID2D1SolidColorBrush> spFillBrush;
			deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(fill, tarPct), &spFillBrush);

			deviceContext->FillRoundedRectangle(&roundedRect, spFillBrush.Get());
		}
		// 渲染边框
		if (shape.frame.has_value())
		{
			COLORREF frame = shape.frame.value().val;
			double tarFramePct = tarPct;
			if (shape.framePct.has_value()) tarFramePct = shape.framePct.value().val;

			ComPtr<ID2D1SolidColorBrush> spBorderBrush;
			deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(frame, tarFramePct), &spBorderBrush);

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			if (shape.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(shape.ft.value().val * tarZoom);
				if (strokeWidth > 0.0F) deviceContext->DrawRoundedRectangle(&roundedRect, spBorderBrush.Get(), strokeWidth);
			}
			else deviceContext->DrawRoundedRectangle(&roundedRect, spBorderBrush.Get(), strokeWidth);
		}
	}

	if (targetRect) BarRenderingAttribute::UnionRectInPlace(*targetRect, BarRenderingAttribute::GetWeigetRect(shape, tarZoom));
	return true;
}
bool BarUIRendering::Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (superellipse.enable.val == false) return false;
	if (!superellipse.fill.has_value() && !superellipse.frame.has_value()) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (superellipse.w.val <= 0 || superellipse.h.val <= 0) return false;
	if (superellipse.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = superellipse.w.val * tarZoom;
	double tarH = superellipse.h.val * tarZoom;
	double tarPct = superellipse.pct.val; // 透明度

	double tarN = 4.0;
	if (superellipse.n.has_value()) tarN = superellipse.n.value().val;

		auto genPoints = [&](float left, float top, float width, float height, float n, int segs)
			{
				const float Pi = 3.14159265359f;
				float radius = min(width, height) / 2.0f;
				float right = left + width;
				float bottom = top + height;
				int cornerSegs = max(6, segs / 4);

				vector<D2D1_POINT_2F> pts;
				pts.reserve(static_cast<size_t>(cornerSegs + 1) * 4 + 1);

				auto appendCorner = [&](float cx, float cy, float begin, float end)
				{
					for (int i = 0; i <= cornerSegs; i++)
					{
						float theta = begin + (end - begin) * static_cast<float>(i) / static_cast<float>(cornerSegs);
						float cosT = cosf(theta);
						float sinT = sinf(theta);
						float x0 = radius * copysignf(powf(abs(cosT), 2.0f / n), cosT);
						float y0 = radius * copysignf(powf(abs(sinT), 2.0f / n), sinT);
						pts.emplace_back(D2D1::Point2F(cx + x0, cy + y0));
					}
				};

				// 非正方形只延长四角之间的直边，圆角始终使用相同的宽高，避免整体拉伸。
				appendCorner(right - radius, top + radius, -Pi / 2.0f, 0.0f);
				appendCorner(right - radius, bottom - radius, 0.0f, Pi / 2.0f);
				appendCorner(left + radius, bottom - radius, Pi / 2.0f, Pi);
				appendCorner(left + radius, top + radius, Pi, Pi * 3.0f / 2.0f);
				pts.emplace_back(pts[0]); // 闭合

			return pts;
		};

	auto toBeziers = [](const vector<D2D1_POINT_2F>& pts, float tension = 1.0f)
		{
			// Catmull-Rom到Bezier转换，首尾闭合
			vector<D2D1_BEZIER_SEGMENT> beziers;
			int N = static_cast<int>(pts.size()) - 1; // pts已闭合，最后一个是等于第一个
			if (N < 3) return beziers;

			for (int i = 0; i < N; i++)
			{
				D2D1_POINT_2F p0 = pts[(i - 1 + N) % N];
				D2D1_POINT_2F p1 = pts[i];
				D2D1_POINT_2F p2 = pts[(i + 1) % N];
				D2D1_POINT_2F p3 = pts[(i + 2) % N];

				D2D1_BEZIER_SEGMENT seg;
				seg.point1 =
				{
					p1.x + (p2.x - p0.x) / 6.0f * tension,
					p1.y + (p2.y - p0.y) / 6.0f * tension
				};
				seg.point2 =
				{
					p2.x - (p3.x - p1.x) / 6.0f * tension,
					p2.y - (p3.y - p1.y) / 6.0f * tension
				};
				seg.point3 = p2;

				beziers.push_back(seg);
			}
			return beziers;
		};

	// 计算边框路径
	int segs = clamp(static_cast<int>((tarW + tarH) / 8.0), 24, 128);
	vector<D2D1_POINT_2F> pts = genPoints(static_cast<float>(tarX), static_cast<float>(tarY), static_cast<float>(tarW), static_cast<float>(tarH), static_cast<float>(tarN), segs);
	vector<D2D1_BEZIER_SEGMENT> beziers = toBeziers(pts);
	if (beziers.empty()) return false;

	ComPtr<ID2D1PathGeometry> geometry;
	d2dFactory1->CreatePathGeometry(&geometry);

	{
		ComPtr<ID2D1GeometrySink> sink;
		geometry->Open(&sink);
		sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
		sink->AddBeziers(beziers.data(), static_cast<UINT32>(beziers.size()));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		sink->Close();
	}

	// Clip
	if (clip)
	{
		ComPtr<ID2D1SolidColorBrush> spFillBrush;
		deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(RGB(0, 0, 0), 0.0), &spFillBrush);

		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillGeometry(geometry.Get(), spFillBrush.Get());
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}

	// 渲染到 DC
	{
		// 渲染填充
		if (superellipse.fill.has_value())
		{
			COLORREF fill = superellipse.fill.value().val;

			ComPtr<ID2D1SolidColorBrush> spFillBrush;
			deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(fill, tarPct), &spFillBrush);

			deviceContext->FillGeometry(geometry.Get(), spFillBrush.Get());
		}
		// 渲染边框
		if (superellipse.frame.has_value())
		{
			COLORREF frame = superellipse.frame.value().val;
			double tarFramePct = tarPct;
			if (superellipse.framePct.has_value()) tarFramePct = superellipse.framePct.value().val;

			ComPtr<ID2D1SolidColorBrush> spBorderBrush;
			deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(frame, tarFramePct), &spBorderBrush);

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			if (superellipse.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(superellipse.ft.value().val * tarZoom);
				if (strokeWidth > 0.0F) deviceContext->DrawGeometry(geometry.Get(), spBorderBrush.Get(), strokeWidth);
			}
			else deviceContext->DrawGeometry(geometry.Get(), spBorderBrush.Get(), strokeWidth);
		}
	}

	if (targetRect) BarRenderingAttribute::UnionRectInPlace(*targetRect, BarRenderingAttribute::GetWeigetRect(superellipse, tarZoom));
	return true;
}
bool BarUIRendering::Svg(ID2D1DeviceContext* deviceContext, BarUiSVGClass& svg, const BarUiInheritClass& inh)
{
	// 判断是否启用
	if (svg.enable.val == false) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (svg.w.val <= 0 || svg.h.val <= 0) return false;
	if (svg.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = svg.w.val * tarZoom;
	double tarH = svg.h.val * tarZoom;
	double tarPct = svg.pct.val; // 透明度

	// 获取绘制缓存
	ComPtr<ID2D1Bitmap> d2dBitmap;
	{
		bool needUpdate = false;
		if (svg.cW != tarW || svg.cH != tarH) needUpdate = true;
		if (svg.color1.has_value() && svg.cColor1 != svg.color1.value().val) needUpdate = true;
		if (svg.color2.has_value() && svg.cColor2 != svg.color2.value().val) needUpdate = true;

		// TODO 优化：可选动画过程中不更新缓存
		if (needUpdate || !svg.cacheBitmap)
		{
			if (!svg.CacheBitmap(deviceContext, tarW, tarH))
				return false;
		}
		d2dBitmap = svg.cacheBitmap.Get();
	}

	// 渲染到 DC
	{
		D2D1_RECT_F destRect = D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY), static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH));
		deviceContext->DrawBitmap(
				d2dBitmap.Get(),
			destRect,								// 目标矩形
			static_cast<FLOAT>(tarPct),				// 不透明度
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
			nullptr									// 源rect, null表示全部
		);
	}

	return true;
}
bool BarUIRendering::Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_ALIGNMENT textAlign)
{
	// 判断是否启用
	if (word.enable.val == false) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (word.size.val <= 0) return false;
	if (word.w.val <= 0 || word.h.val <= 0) return false;
	if (word.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = word.w.val * tarZoom;
	double tarH = word.h.val * tarZoom;
	double tarSize = word.size.val * tarZoom;
	double tarPct = word.pct.val; // 透明度

	// Word 控件改为存入 wstring
	wstring tarContent = word.content.GetVal();

	// 获取样式
	IDWriteTextFormat* textFormat = nullptr;
	{
		/*IDWriteTextFormat* tmpTextFormat;
		dWriteFactory1->CreateTextFormat(
			L"HarmonyOS Sans SC",
			dWriteFontCollection.Get(),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			static_cast<FLOAT>(tarSize),
			L"zh-cn",
			&tmpTextFormat
		);
		tmpTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		tmpTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

		textFormat.Attach(tmpTextFormat);*/

		textFormat = barUISetClass->barMedia.formatCache->GetFormat(
			L"HarmonyOS Sans SC",
			tarSize,
			dWriteFontCollection.Get(),
			fontWeight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			L"zh-cn",
			textAlign,
			DWRITE_PARAGRAPH_ALIGNMENT_CENTER   // 指定段落居中
		);
	}
	// 计算区域
	D2D1_RECT_F layoutRect;
	{
		layoutRect = D2D1::RectF(
			static_cast<FLOAT>(tarX),
			static_cast<FLOAT>(tarY),
			static_cast<FLOAT>(tarX + tarW),
			static_cast<FLOAT>(tarY + tarH)
		);
	}
	// 渲染到 DC
	{
		COLORREF color = word.color.val;

		ComPtr<ID2D1SolidColorBrush> spFillBrush;
		deviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(color, tarPct), &spFillBrush);

		deviceContext->DrawTextW(
			tarContent.c_str(),
			wcslen(tarContent.c_str()),
			textFormat,
			layoutRect,
			spFillBrush.Get(),
			D2D1_DRAW_TEXT_OPTIONS_CLIP
		);
	}

	return true;
}

// UI 总集

// 渲染
void BarUISetClass::Rendering()
{
	Inkeys::Thread::StatusGuard guard("BarUISetClass::Rendering");

	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;
	}
	SIZE sizeWnd = { static_cast<LONG>(barWindow.w), static_cast<LONG>(barWindow.h) };
	POINT ptSrc = { 0,0 };
	POINT ptDst = { 0,0 };
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	{
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = NULL;
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
	}

	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	// 初始化 D2D DC
	ComPtr<ID2D1DeviceContext>				barDeviceContext;
	ComPtr<ID2D1Bitmap1>					barBackgroundBitmap;
	ComPtr<ID2D1GdiInteropRenderTarget>	barGdiInterop;
	{
		HRESULT hr = d2dDevice_WARP->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &barDeviceContext);
		if (FAILED(hr))
		{
			if (IDTLogger) IDTLogger->error("[BarUISetClass::Rendering] CreateDeviceContext 失败, hr=0x{:08X}", static_cast<unsigned int>(hr));
			return;
		}

		D2D1_BITMAP_PROPERTIES1 bitmapProperties =
			D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
			);

		D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(barWindow.w), static_cast<UINT32>(barWindow.h));

		hr = barDeviceContext->CreateBitmap(
			size,
			nullptr,
			0,
			&bitmapProperties,
			&barBackgroundBitmap
		);
		if (FAILED(hr))
		{
			if (IDTLogger) IDTLogger->error("[BarUISetClass::Rendering] CreateBitmap 失败, hr=0x{:08X}", static_cast<unsigned int>(hr));
			return;
		}

		hr = barDeviceContext.As(&barGdiInterop);
		if (FAILED(hr))
		{
			if (IDTLogger) IDTLogger->error("[BarUISetClass::Rendering] 获取 ID2D1GdiInteropRenderTarget 失败, hr=0x{:08X}", static_cast<unsigned int>(hr));
			return;
		}

		barDeviceContext->SetTarget(barBackgroundBitmap.Get());
		barDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	}

	chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();
	chrono::high_resolution_clock::time_point animationReckon = reckon;
	RECT original = RECT(0, 0, barWindow.w, barWindow.h), current = RECT(0, 0, 0, 0);
	// 独立记录渲染侧已经处理的主栏方向，不能使用动画 tar 的符号代替布局状态。
	bool mainBarLayoutSide = barState.widgetPosition.mainBar;
	bool drawAttributeLayoutSide = barState.widgetPosition.primaryBar;
	bool drawAttributeLayoutOpen = barState.drawAttribute;
	BarUiTimelineClass mainBarTimeline;
	BarUiTimelineClass drawAttributeTimeline;
	BarUiCurveEnum mainBarBatchCurve = BarUiCurveEnum::EaseInOutCubic;
	optional<double> mainBarLayoutWidth;
	// 粗细预览使用独立动画值；切换画笔类型时曲线与数字共用同一进度。
	BarUiValueClass drawAttributePenThickness(max(0.0f, GetPenWidth()));
	constexpr double mainButtonScale = 1.05;
	constexpr double mainButtonBaseSize = 80.0;
	auto mainButtonLogo = svgMap[BarUISetSvgEnum::logo1];
	double mainButtonLogoBaseW = mainButtonLogo->w.tar;
	double mainButtonLogoBaseH = mainButtonLogo->h.tar;
	unsigned long long handledMainButtonPulseSerial = 0;

	wstring fps;
	for (int forNum = 1; !offSignal; forNum = 2)
	{
	#pragma region 计算UI

		auto animationNow = chrono::high_resolution_clock::now();
		double animationDtSeconds = chrono::duration<double>(animationNow - animationReckon).count();
		animationReckon = animationNow;
		if (!isfinite(animationDtSeconds) || animationDtSeconds < 0.0) animationDtSeconds = 0.0;
		animationDtSeconds = clamp(animationDtSeconds, 0.0, 0.05); // 防止调试或休眠恢复后一帧跳太远

		// 主按钮
		{
			double operationDur = BarUiDefaultOperationDur;
			auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
			unsigned long long mainButtonPulseSerial = mainButtonClickPulseSerial.load(std::memory_order_relaxed);
			bool mainButtonPulse = mainButtonPulseSerial != handledMainButtonPulseSerial;
			if (mainButtonPulse) handledMainButtonPulseSerial = mainButtonPulseSerial;

			const BarUiCurveSpecClass mainButtonPulseCurve{
				BarUiCurveEnum::EaseOutBack, BarUiCurveEnum::EaseInBack, 0.0, false };
			if (mainButtonPulse)
			{
				// 有效点击只在松手后触发一次放大关键帧，主图标与超椭圆同步回到原尺寸。
				mainButton->w.SetTar(mainButtonBaseSize, operationDur,
					mainButtonBaseSize * mainButtonScale, true, mainButtonPulseCurve);
				mainButton->h.SetTar(mainButtonBaseSize, operationDur,
					mainButtonBaseSize * mainButtonScale, true, mainButtonPulseCurve);
				mainButtonLogo->w.SetTar(mainButtonLogoBaseW, operationDur,
					mainButtonLogoBaseW * mainButtonScale, true, mainButtonPulseCurve);
				mainButtonLogo->h.SetTar(mainButtonLogoBaseH, operationDur,
					mainButtonLogoBaseH * mainButtonScale, true, mainButtonPulseCurve);
			}
			else
			{
				mainButton->w.SetTar(mainButtonBaseSize, operationDur);
				mainButton->h.SetTar(mainButtonBaseSize, operationDur);
				mainButtonLogo->w.SetTar(mainButtonLogoBaseW, operationDur);
				mainButtonLogo->h.SetTar(mainButtonLogoBaseH, operationDur);
			}

			BarUiCurveEnum mainButtonPctCurve = barState.fold
				? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine;
			BarUiCurveSpecClass mainButtonPctCurveSpec{
				mainButtonPctCurve, mainButtonPctCurve, 0.0, false };
			if (barState.fold)
			{
				mainButton->n.value().SetTar(3.0, operationDur);

				mainButton->pct.SetTar(
					0.6, operationDur, nullopt, false, mainButtonPctCurveSpec);
			}
			else
			{
				mainButton->n.value().SetTar(10.0, operationDur);

				mainButton->pct.SetTar(
					0.8, operationDur, nullopt, false, mainButtonPctCurveSpec);
			}
		}
		// 主栏
		{
			double operationDur = BarUiDefaultOperationDur;
			auto mainBar = shapeMap[BarUISetShapeEnum::MainBar];
			BarUiCurveSpecClass syncedValueCurve;
			bool syncValueCurveFromBatch = false;
			BarUiCurveSpecClass syncedPctCurve{
				BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
			const BarUiCurveSpecClass keyframeValueCurve{
				BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack, 0.0, false };
			const BarUiCurveSpecClass keyframePctCurve{
				BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
			auto SyncValueDuration = [&](BarUiValueClass& value)
				{
					// 只在新目标刚建立时提交批次剩余时长，不能每帧改写正在推进的动画段。
					if (!value.IsSame() && value.progress == 0.0)
					{
						value.dur = operationDur;
						// 关键帧已经携带前后两段曲线；普通值仅在批次中覆盖自身默认曲线。
						if (!value.hasMiddleV)
						{
							BarUiCurveSpecClass curveSpec = syncValueCurveFromBatch
								? syncedValueCurve
								: BarUiCurveSpecClass{ value.curve, value.curve, 0.0, false };
							value.activeCurve = curveSpec.first;
							value.activeMiddleCurve = curveSpec.second;
							value.timelineStartProgress = curveSpec.timelineStartProgress;
							value.continueTimelinePhase = curveSpec.continueTimelinePhase;
						}
					}
				};
			auto SyncPctDuration = [&](BarUiPctClass& pct)
				{
					if (!pct.IsSame() && pct.progress == 0.0)
					{
						pct.dur = operationDur;
						pct.activeCurve = syncedPctCurve.first;
						pct.activeMiddleCurve = syncedPctCurve.second;
						pct.timelineStartProgress = syncedPctCurve.timelineStartProgress;
						pct.continueTimelinePhase = syncedPctCurve.continueTimelinePhase;
					}
				};
			bool currentMainBarSide = barState.widgetPosition.mainBar;
			bool mainBarSideSwitch = !barState.fold && currentMainBarSide != mainBarLayoutSide;
			// 换边动画被打断时，新一侧仍会在下一帧与这里记录的旧侧产生一次明确变化。
			mainBarLayoutSide = currentMainBarSide;
			bool currentDrawAttributeSide = barState.widgetPosition.primaryBar;
			bool drawAttributeSideSwitch = barState.drawAttribute
				&& currentDrawAttributeSide != drawAttributeLayoutSide;
			drawAttributeLayoutSide = currentDrawAttributeSide;
			bool currentDrawAttributeOpen = barState.drawAttribute;
			bool drawAttributeVisibilityChange = currentDrawAttributeOpen != drawAttributeLayoutOpen;
			drawAttributeLayoutOpen = currentDrawAttributeOpen;
			if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
				drawAttributePenThickness.SetTar(max(0.0f, GetPenWidth()), operationDur);
			bool mainBarFoldChange = (barState.fold && mainBar->x.tar != 0.0)
				|| (!barState.fold && mainBar->x.tar == 0.0);
			auto CalculateButtonLayoutWidth = [&]()
				{
					double width = 5.0, xO = 5.0, yO = 5.0;
					for (int id = 0; id < barButtomSet.tot; id++)
					{
						BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
						if (!temp) continue;
						if (temp->size == BarButtomSizeEnum::oneOne)
						{
							if (temp->hide) continue;
							if (yO <= 5.0) yO += 37.5, width += 37.5;
							else if (xO + 37.5 >= width) xO += 37.5, yO = 5.0;
							else xO += 37.5;
						}
						else if (temp->size == BarButtomSizeEnum::twoOne)
						{
							if (yO > 5.0 && xO + 75.0 > width) xO = width, yO = 5.0;
							if (temp->hide) continue;
							if (yO <= 5.0) yO += 37.5, width += 75.0;
							else xO += 75.0, yO = 5.0;
						}
						else if (temp->size == BarButtomSizeEnum::twoTwo)
						{
							if (yO > 5.0) yO = 5.0, xO = width;
							if (!temp->hide) xO += 75.0, width += 75.0;
						}
						else if (temp->size == BarButtomSizeEnum::oneTwo)
						{
							if (yO > 5.0) xO = width;
							if (!temp->hide) xO += 15.0, yO = 5.0, width += 15.0;
						}
					}
					return width;
				};
			double layoutTotalWidth = CalculateButtonLayoutWidth();
			bool mainBarLayoutChange = mainBarLayoutWidth.has_value()
				&& abs(layoutTotalWidth - mainBarLayoutWidth.value()) > 0.000001;
			bool mainBarLayoutExpands = mainBarLayoutChange
				&& layoutTotalWidth > mainBarLayoutWidth.value();
			// 新操作创建完整批次；已有批次中的布局重算只继承剩余时间，不延后结束时刻。
			bool restartMainBarTimeline = mainBarFoldChange || mainBarSideSwitch
				|| (!barState.fold && !mainBarTimeline.IsActive() && mainBarLayoutChange);
			if (restartMainBarTimeline)
			{
				if (mainBarSideSwitch) mainBarBatchCurve = BarUiCurveEnum::EaseInOutCubic;
				else if (mainBarFoldChange)
					mainBarBatchCurve = barState.fold
					? BarUiCurveEnum::EaseInBack : BarUiCurveEnum::EaseOutBack;
				else mainBarBatchCurve = mainBarLayoutExpands
					? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack;
				mainBarTimeline.Restart(operationDur);
			}
			else if (mainBarTimeline.IsActive() && mainBarLayoutChange)
			{
				// 批次中途反向改变布局时只更换新目标曲线，仍沿用原截止时间。
				mainBarBatchCurve = mainBarLayoutExpands
					? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack;
			}
			mainBarLayoutWidth = layoutTotalWidth;
			if (mainBarTimeline.IsActive()) operationDur = mainBarTimeline.GetRemainingDuration();
			double mainBarPhase = mainBarTimeline.IsActive() ? mainBarTimeline.GetProgress() : 0.0;
			bool continueMainBarPhase = mainBarTimeline.IsActive() && mainBarPhase > 0.0;
			syncValueCurveFromBatch = mainBarTimeline.IsActive();
			BarUiCurveEnum syncedMainBarCurve = mainBarTimeline.IsActive()
				? mainBarBatchCurve : BarUiCurveEnum::EaseInOutCubic;
			BarUiCurveEnum syncedMainBarPctCurve = mainBarTimeline.IsActive()
				&& mainBarBatchCurve == BarUiCurveEnum::EaseInBack
				? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine;
			syncedValueCurve = { syncedMainBarCurve, syncedMainBarCurve,
				mainBarPhase, continueMainBarPhase };
			syncedPctCurve = { syncedMainBarPctCurve, syncedMainBarPctCurve,
				mainBarPhase, continueMainBarPhase };
			auto SetButtonPositionTar = [&](BarUiValueClass& value, double target, double middle, bool mirrorX = false)
				{
					// 左侧展开仍按正序布局，只将最终横坐标按主栏宽度镜像。
					if (mirrorX && !barState.widgetPosition.mainBar) target = layoutTotalWidth - target;
					if (mainBarSideSwitch)
					{
						value.SetTar(target, operationDur, middle, true, keyframeValueCurve);
					}
					else value.SetTar(target, operationDur, nullopt, mainBarFoldChange, syncedValueCurve);
				};

			// 按钮位置计算（特别操作）
			double totalWidth = 5.0;
			{
				double xO = 5.0, yO = 5.0;
				// 控件计算的 xO 和 yO 包含自身和 右侧、下册 的空隙值 5px

				// 两侧始终按正序计算；向左展开时由横坐标镜像实现从右向左填充。
				auto baseRange = views::iota(0, barButtomSet.tot);
				variant<decltype(baseRange), decltype(baseRange | views::reverse)> viewVariant;
				viewVariant = baseRange;

				visit([&](auto&& forRange)
					{
						for (int id : forRange)
						{
							BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
							if (temp == nullptr) continue;
							if (temp->icon.color1.has_value())
							{
								COLORREF lightColor = temp->size == BarButtomSizeEnum::oneOne
									|| temp->size == BarButtomSizeEnum::oneTwo
									? RGB(0, 0, 0) : RGB(27, 27, 27);
								COLORREF iconColor = temp->state->state == BarWidgetState::Selected
									? RGB(88, 255, 236)
									: (barStyle.darkStyle ? RGB(255, 255, 255) : lightColor);
								// 第一次计算或不可见时直接同步，避免 SVG 显示后才从黑色过渡。
								if (forNum == 1 || barState.fold || temp->hide)
									temp->icon.color1.value().SetDirect(iconColor);
								else temp->icon.color1.value().SetTar(iconColor);
							}

							if (temp->size == BarButtomSizeEnum::oneOne)
							{
								// 特殊设定：是否是颜色选择器
								bool isColorSelector = (temp->name.enable.tar && temp->name.content.GetTar().substr(0, 7) == L"__color");

								if (temp->buttom.enable.tar)
								{
									if (barState.fold || temp->hide)
									{
										if (barState.fold)
										{
											SetButtonPositionTar(temp->buttom.x, 40.0, 40.0);
											SetButtonPositionTar(temp->buttom.y, 40.0, 40.0);
										}

										temp->buttom.pct.SetTar(0.0, operationDur);
									}
									else
									{
									SetButtonPositionTar(temp->buttom.x, xO + 15.0, 40.0, true);
									if (yO <= 5.0) SetButtonPositionTar(temp->buttom.y, yO + 17.5, 40.0); // 位于第一行
									else SetButtonPositionTar(temp->buttom.y, yO + 15.0, 40.0); // 位于第二行

										if (isColorSelector) temp->buttom.pct.SetTar(1.0, operationDur); // 只有颜色选择器使用
										else
										{
											if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.SetTar(0.1, operationDur);
											else if (temp->state->state == BarWidgetState::Selected) temp->buttom.pct.SetTar(0.2, operationDur);
											else temp->buttom.pct.SetTar(0.0, operationDur);
										}
									}
								temp->buttom.w.SetTar(30.0, operationDur);
								temp->buttom.h.SetTar(30.0, operationDur);

									if (!isColorSelector)
									{
										if (temp->state->emph == BarWidgetEmphasize::Pressed && temp->state->state != BarWidgetState::Selected)
											temp->buttom.fill.value().SetTar(RGB(127, 127, 127));
										else temp->buttom.fill.value().SetTar(RGB(88, 255, 236));
									}
								}
								if (temp->icon.enable.tar)
								{
									if (isColorSelector) temp->icon.SetWH(nullopt, 10.0); // 颜色选择器中的图标即为标识选中该颜色，所以需要较小尺寸
									else temp->icon.SetWH(nullopt, 20.0);

									temp->icon.x.SetTar(0.0);
									temp->icon.y.SetTar(0.0);
									if (barState.fold || temp->hide)
									{
										temp->icon.pct.SetTar(0.0, operationDur);
									}
									else
									{
										temp->icon.pct.SetTar(1.0, operationDur);
									}
								}
								if (temp->name.enable.tar)
								{
									// 无法容下文字的位置
									temp->name.pct.SetTar(0.0, operationDur);
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.SetTar(0.0, operationDur);
									temp->icon.pct.SetTar(0.0, operationDur);
									temp->name.pct.SetTar(0.0, operationDur);
								}
								else
								{
									// 位于第一行
									if (yO <= 5.0)
									{
										yO += 37.5;
										totalWidth += 37.5;
										// 只有在第一行时才增加总宽度，因为第二行没有再加的必要
										// 如果第二行是 twoOne 或 twoTwo 的按钮，则会自动换行到更右侧
									}
									// 位于第二行
									else
									{
										// 如果第一行是 twoOne，现在是第二行应该存在塞下第二个 1*1 的按钮的情况

										if (xO + 37.5 >= totalWidth)
										{
											// 如果当前 xO + 37.5 超过了总宽度，则换行到更右侧
											xO += 37.5;
											yO = 5.0;
										}
										else
										{
											// 否则继续在当前行
											xO += 37.5;
										}
									}
								}
							}
							if (temp->size == BarButtomSizeEnum::twoOne)
							{
								if (yO > 5.0)
								{
									// 如果当前位置处于第二行，且容不下一个 2*1 的按钮，则换行到更右侧
									if (xO + 75.0 > totalWidth)
									{
										xO = totalWidth;
										yO = 5.0;
									}
								}

								if (temp->buttom.enable.tar)
								{
									if (barState.fold || temp->hide)
									{
										if (barState.fold)
										{
											SetButtonPositionTar(temp->buttom.x, 40.0, 40.0);
											SetButtonPositionTar(temp->buttom.y, 40.0, 40.0);
										}

										temp->buttom.pct.SetTar(0.0, operationDur);
									}
									else
									{
									SetButtonPositionTar(temp->buttom.x, xO + 35.0, 40.0, true);
									if (yO <= 5.0) SetButtonPositionTar(temp->buttom.y, yO + 17.5, 40.0); // 位于第一行
									else SetButtonPositionTar(temp->buttom.y, yO + 15.0, 40.0); // 位于第二行

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.SetTar(0.1, operationDur);
										else if (temp->state->state == BarWidgetState::Selected) temp->buttom.pct.SetTar(0.2, operationDur);
										else temp->buttom.pct.SetTar(0.0, operationDur);
									}
								temp->buttom.w.SetTar(70.0, operationDur);
								temp->buttom.h.SetTar(30.0, operationDur);

									if (temp->state->emph == BarWidgetEmphasize::Pressed && temp->state->state != BarWidgetState::Selected)
										temp->buttom.fill.value().SetTar(RGB(127, 127, 127));
									else temp->buttom.fill.value().SetTar(RGB(88, 255, 236));
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 18.0);

									temp->icon.x.SetTar(-21.0); // 靠左对齐（上下两侧均保持 6px 的空隙，而左侧是 5px）
									temp->icon.y.SetTar(0.0);
									if (barState.fold || temp->hide) temp->icon.pct.SetTar(0.0, operationDur);
									else
									{
										temp->icon.pct.SetTar(1.0, operationDur);
									}
								}
								if (temp->name.enable.tar)
								{
									temp->name.x.SetTar(11.5); // 右对齐
									temp->name.y.SetTar(0.0);
									temp->name.w.SetTar(37); // 70px 宽度中除去左侧 icon 占用的 18px + 5px * 2 的空隙,考虑自身右侧还有 5px 的间隙
									temp->name.h.SetTar(30.0);
									if (barState.fold || temp->hide) temp->name.pct.SetTar(0.0, operationDur);
									else temp->name.pct.SetTar(1.0, operationDur);

									if (barStyle.darkStyle)
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.SetTar(RGB(88, 255, 236));
										else temp->name.color.SetTar(RGB(255, 255, 255));
									}
									else
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.SetTar(RGB(88, 255, 236));
										else temp->name.color.SetTar(RGB(27, 27, 27));
									}
									temp->name.size.SetTar(12.0);
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.SetTar(0.0, operationDur);
									temp->icon.pct.SetTar(0.0, operationDur);
									temp->name.pct.SetTar(0.0, operationDur);
								}
								else
								{
									// 位于第一行
									if (yO <= 5.0)
									{
										yO += 37.5;
										totalWidth += 75.0;
										// 只在第一行中增加总宽度，因为第二行没有再加的必要
										// 第二行如果是 oneOne 的按钮，那么在超过宽度时也会自动换行到更右侧
									}
									// 位于第二行
									else
									{
										xO += 75.0;
										yO = 5.0;
									}
								}
							}
							if (temp->size == BarButtomSizeEnum::twoTwo)
							{
								if (yO > 5.0)
								{
									yO = 5.0;
									xO = totalWidth;
								}

								if (temp->buttom.enable.tar)
								{
									if (barState.fold || temp->hide)
									{
										if (barState.fold)
										{
											SetButtonPositionTar(temp->buttom.x, 40.0, 40.0);
											SetButtonPositionTar(temp->buttom.y, 40.0, 40.0);
										}

										temp->buttom.pct.SetTar(0.0, operationDur);
									}
									else
									{
									SetButtonPositionTar(temp->buttom.x, xO + 35.0, 40.0, true);
									SetButtonPositionTar(temp->buttom.y, yO + 35.0, 40.0);

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.SetTar(0.1, operationDur);
										else if (temp->state->state == BarWidgetState::Selected) temp->buttom.pct.SetTar(0.2, operationDur);
										else temp->buttom.pct.SetTar(0.0, operationDur);
									}
								temp->buttom.w.SetTar(70.0, operationDur);
								temp->buttom.h.SetTar(70.0, operationDur);

									if (temp->state->emph == BarWidgetEmphasize::Pressed && temp->state->state != BarWidgetState::Selected)
										temp->buttom.fill.value().SetTar(RGB(127, 127, 127));
									else temp->buttom.fill.value().SetTar(RGB(88, 255, 236));
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 28.0);
									temp->icon.x.SetTar(0.0);
									temp->icon.y.SetTar(-10.0);
									if (barState.fold || temp->hide)
									{
										temp->icon.pct.SetTar(0.0, operationDur);
									}
									else
									{
										temp->icon.pct.SetTar(1.0, operationDur);
									}
								}
								if (temp->name.enable.tar)
								{
									temp->name.x.SetTar(0.0);
									temp->name.y.SetTar(20.0);
									temp->name.w.SetTar(70.0);
									temp->name.h.SetTar(25.0);
									if (barState.fold || temp->hide) temp->name.pct.SetTar(0.0, operationDur);
									else temp->name.pct.SetTar(1.0, operationDur);

									if (barStyle.darkStyle)
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.SetTar(RGB(88, 255, 236));
										else temp->name.color.SetTar(RGB(255, 255, 255));
									}

									else
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.SetTar(RGB(88, 255, 236));
										else temp->name.color.SetTar(RGB(27, 27, 27));
									}

									temp->name.size.SetTar(13.0);
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.SetTar(0.0, operationDur);
									temp->icon.pct.SetTar(0.0, operationDur);
									temp->name.pct.SetTar(0.0, operationDur);
								}
								else
								{
									xO += 75, yO = 5.0;
									totalWidth += 75;
								}
							}

							// 特殊体质 - 分隔栏
							if (temp->size == BarButtomSizeEnum::oneTwo)
							{
								if (yO > 5.0) xO = totalWidth;

								if (temp->buttom.enable.tar)
								{
									if (barState.fold || temp->hide)
									{
										if (barState.fold)
										{
											SetButtonPositionTar(temp->buttom.x, 40.0, 40.0);
											SetButtonPositionTar(temp->buttom.y, 40.0, 40.0);
										}

										temp->buttom.pct.SetTar(0.0, operationDur);
									}
									else
									{
									SetButtonPositionTar(temp->buttom.x, xO + 5.0, 40.0, true);
									SetButtonPositionTar(temp->buttom.y, yO + 35.0, 40.0);

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.SetTar(0.2, operationDur);
										else temp->buttom.pct.SetTar(0.0, operationDur);
									}
								temp->buttom.w.SetTar(10.0, operationDur);
								temp->buttom.h.SetTar(70.0, operationDur);

									if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.fill.value().SetTar(RGB(127, 127, 127));
									else temp->buttom.fill.value().SetTar(RGB(88, 255, 236));
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 60.0);
									if (barState.fold || temp->hide)
									{
										temp->icon.pct.SetTar(0.0, operationDur);
									}
									else
									{
										temp->icon.pct.SetTar(0.18, operationDur);
									}
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.SetTar(0.0, operationDur);
									temp->icon.pct.SetTar(0.0, operationDur);
									temp->name.pct.SetTar(0.0, operationDur);
								}
								else
								{
									xO += 15, yO = 5.0;
									totalWidth += 15;
								}
							}

							// 尺寸枚举只负责选择布局，按钮及其内容统一在同一过程时间内到达新布局。
							SyncValueDuration(temp->buttom.x);
							SyncValueDuration(temp->buttom.y);
							SyncValueDuration(temp->buttom.w);
							SyncValueDuration(temp->buttom.h);
							SyncPctDuration(temp->buttom.pct);
							SyncValueDuration(temp->icon.x);
							SyncValueDuration(temp->icon.y);
							SyncValueDuration(temp->icon.w);
							SyncValueDuration(temp->icon.h);
							SyncPctDuration(temp->icon.pct);
							SyncValueDuration(temp->name.x);
							SyncValueDuration(temp->name.y);
							SyncValueDuration(temp->name.w);
							SyncValueDuration(temp->name.h);
							SyncValueDuration(temp->name.size);
							SyncPctDuration(temp->name.pct);

							if (mainBarSideSwitch)
							{
								// 换边中点将整个按钮组合隐藏，再从主按钮下方展开到新位置。
								temp->buttom.pct.SetTar(temp->buttom.pct.tar, operationDur, 0.0, true, keyframePctCurve);
								temp->icon.pct.SetTar(temp->icon.pct.tar, operationDur, 0.0, true, keyframePctCurve);
								temp->name.pct.SetTar(temp->name.pct.tar, operationDur, 0.0, true, keyframePctCurve);
							}
						}
					}, viewVariant);

				auto AnchorHiddenButton = [&](BarButtomPresetEnum hiddenPreset, BarButtomPresetEnum anchorPreset)
					{
						BarButtomClass* hidden = barButtomSet.preset[static_cast<int>(hiddenPreset)];
						BarButtomClass* anchor = barButtomSet.preset[static_cast<int>(anchorPreset)];
						if (barState.fold || !hidden || !anchor || !hidden->hide) return;

						// 隐藏控件停在来源按钮中心，显示时从该位置展开。
						SetButtonPositionTar(hidden->buttom.x, anchor->buttom.x.tar, 40.0);
						SetButtonPositionTar(hidden->buttom.y, anchor->buttom.y.tar, 40.0);
						hidden->lastDrawX = anchor->buttom.x.tar;
						hidden->lastDrawY = anchor->buttom.y.tar;
					};
				AnchorHiddenButton(BarButtomPresetEnum::Eraser, BarButtomPresetEnum::Draw);
				AnchorHiddenButton(BarButtomPresetEnum::Recall, BarButtomPresetEnum::Draw);
				AnchorHiddenButton(BarButtomPresetEnum::Pierce, BarButtomPresetEnum::Freeze);
			}
			totalWidth = layoutTotalWidth;
			Inkeys::UI::Bar::Zoom::FitInitialAfterMainBarLayout(*this, totalWidth);
			{ /**/ }

			// 主栏
			{
				if (barState.fold)
				{
					mainBar->x.SetTar(0.0, operationDur, nullopt, mainBarFoldChange, syncedValueCurve);
					mainBar->w.SetTar(80.0, operationDur, nullopt, mainBarFoldChange, syncedValueCurve);

					shapeMap[BarUISetShapeEnum::MainBar]->pct.SetTar(0.0, operationDur, nullopt, false, syncedPctCurve);
					shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().SetTar(0.0, operationDur, nullopt, false, syncedPctCurve);
				}
				else
				{
					if (mainBarSideSwitch) mainBar->w.SetTar(totalWidth, operationDur, 80.0, true, keyframeValueCurve);
					else mainBar->w.SetTar(totalWidth, operationDur, nullopt, mainBarFoldChange, syncedValueCurve);

					double targetX = 0.0;
					if (barState.widgetPosition.mainBar)
						targetX = superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + mainBar->w.tar / 2.0 + 10.0;
					else
						targetX = -(superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + mainBar->w.tar / 2.0 + 10.0);

					if (mainBarSideSwitch) mainBar->x.SetTar(targetX, operationDur, 0.0, true, keyframeValueCurve);
					else mainBar->x.SetTar(targetX, operationDur, nullopt, mainBarFoldChange, syncedValueCurve);

					shapeMap[BarUISetShapeEnum::MainBar]->pct.SetTar(0.8, operationDur, nullopt, false, syncedPctCurve);
					shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().SetTar(0.18, operationDur, nullopt, false, syncedPctCurve);
				}
				if (mainBarSideSwitch)
				{
					// 主栏填充和边框在换边关键帧同步变为全透明。
					mainBar->pct.SetTar(mainBar->pct.tar, operationDur, 0.0, true, keyframePctCurve);
					mainBar->framePct.value().SetTar(mainBar->framePct.value().tar, operationDur, 0.0, true, keyframePctCurve);
				}
				if (barStyle.darkStyle)
				{
					shapeMap[BarUISetShapeEnum::MainBar]->fill.value().SetTar(RGB(24, 24, 24));
					shapeMap[BarUISetShapeEnum::MainBar]->frame.value().SetTar(RGB(255, 255, 255));
				}
				else
				{
					shapeMap[BarUISetShapeEnum::MainBar]->fill.value().SetTar(RGB(243, 243, 243));
					shapeMap[BarUISetShapeEnum::MainBar]->frame.value().SetTar(RGB(0, 0, 0));
				}

				// 绘制属性
				{
					bool drawAttributeBatchChange = drawAttributeVisibilityChange || drawAttributeSideSwitch;
					operationDur = BarUiDefaultOperationDur;
					double drawAttributePhase = 0.0;
					bool continueDrawAttributePhase = false;
					if (drawAttributeBatchChange)
					{
						// 主栏批次仍活跃时直接加入其剩余过程，不重启也不延后主栏结束时刻。
						if (mainBarTimeline.IsActive())
						{
							operationDur = mainBarTimeline.GetRemainingDuration();
							drawAttributePhase = mainBarTimeline.GetProgress();
							continueDrawAttributePhase = drawAttributePhase > 0.0;
						}
						drawAttributeTimeline.Restart(operationDur);
					}
					else if (drawAttributeTimeline.IsActive())
					{
						operationDur = drawAttributeTimeline.GetRemainingDuration();
						drawAttributePhase = drawAttributeTimeline.GetProgress();
						continueDrawAttributePhase = drawAttributePhase > 0.0;
					}
					syncValueCurveFromBatch = drawAttributeTimeline.IsActive();
					BarUiCurveEnum drawAttributeCurve = drawAttributeTimeline.IsActive()
						? (barState.drawAttribute ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack)
						: BarUiCurveEnum::EaseInOutCubic;
					BarUiCurveEnum drawAttributePctCurve = drawAttributeTimeline.IsActive()
						&& !barState.drawAttribute
						? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine;
					syncedValueCurve = {
						drawAttributeCurve, drawAttributeCurve,
						drawAttributePhase, continueDrawAttributePhase };
					syncedPctCurve = { drawAttributePctCurve, drawAttributePctCurve,
						drawAttributePhase, continueDrawAttributePhase };
					const BarUiCurveSpecClass drawAttributeKeyframeValueCurve{
						BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack,
						drawAttributePhase, continueDrawAttributePhase };
					const BarUiCurveSpecClass drawAttributeKeyframePctCurve{
						BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine,
						drawAttributePhase, continueDrawAttributePhase };
					constexpr double drawAttributeCompactWidth = 60.0;
					constexpr double drawAttributeCompactScale = drawAttributeCompactWidth / 335.0;
					constexpr double drawAttributeCompactHeight = 120.0 * drawAttributeCompactScale;
					auto CompactDrawAttributeX = [&](double expandedX) { return expandedX * drawAttributeCompactScale; };
					auto CompactDrawAttributeY = [&](double expandedY)
						{
							return expandedY * drawAttributeCompactScale;
						};
					auto CompactDrawAttributeSize = [&](double expandedSize)
						{
							return expandedSize * drawAttributeCompactScale;
						};
					if (!barState.drawAttribute)
					{
						// 收起面板保持与 335×120 展开面板相同宽高比，并居中藏在绘制按钮下方。
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->x.SetTar(0.0);
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->y.SetTar(0.0);
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->w.SetTar(drawAttributeCompactWidth);
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->h.SetTar(drawAttributeCompactHeight);

						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->pct.SetTar(0.0);
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->framePct.value().SetTar(0.0);
					}
					else
					{
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->w.SetTar(335.0);
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->h.SetTar(120.0);

						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->x.SetTar(0);
						if (barState.widgetPosition.primaryBar)
							shapeMap[BarUISetShapeEnum::DrawAttributeBar]->y.SetTar((shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + shapeMap[BarUISetShapeEnum::DrawAttributeBar]->GetH() / 2.0 + 10.0));
						else
							shapeMap[BarUISetShapeEnum::DrawAttributeBar]->y.SetTar(-(shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + shapeMap[BarUISetShapeEnum::DrawAttributeBar]->GetH() / 2.0 + 10.0));

						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->pct.SetTar(0.9);
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->framePct.value().SetTar(0.18);
					}
					if (barStyle.darkStyle)
					{
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->fill.value().SetTar(RGB(24, 24, 24));
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->frame.value().SetTar(RGB(255, 255, 255));
					}
					else
					{
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->fill.value().SetTar(RGB(243, 243, 243));
						shapeMap[BarUISetShapeEnum::DrawAttributeBar]->frame.value().SetTar(RGB(0, 0, 0));
					}

					// Color 区域
					{
						// Color 1
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.SetTar(CompactDrawAttributeX(5.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(CompactDrawAttributeY(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.SetTar(5.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->fill.value().tar))
							{
								// 说明当前选中的是当前的颜色
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().SetTar(1.0);
							}
						}
						// Color 2
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.SetTar(CompactDrawAttributeX(5.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(CompactDrawAttributeY(40.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(CompactDrawAttributeY(85.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.SetTar(5.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(40.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(85.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().SetTar(1.0);
							}
						}
						// Color 3
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.SetTar(CompactDrawAttributeX(40.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(CompactDrawAttributeY(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.SetTar(40.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().SetTar(1.0);
							}
						}
						// Color 4
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.SetTar(CompactDrawAttributeX(40.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(CompactDrawAttributeY(40.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(CompactDrawAttributeY(85.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.SetTar(40.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(40.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(85.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().SetTar(1.0);
							}
						}
						// Color 5
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.SetTar(CompactDrawAttributeX(75.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(CompactDrawAttributeY(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.SetTar(75.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().SetTar(1.0);
							}
						}
						// Color 6
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.SetTar(CompactDrawAttributeX(75.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(CompactDrawAttributeY(40.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(CompactDrawAttributeY(85.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.SetTar(75.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(40.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(85.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().SetTar(1.0);
							}
						}
						// Color 7
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.SetTar(CompactDrawAttributeX(110.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(CompactDrawAttributeY(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.SetTar(110.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().SetTar(1.0);
							}
						}
						// Color 8
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.SetTar(CompactDrawAttributeX(110.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(CompactDrawAttributeY(40.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(CompactDrawAttributeY(85.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.SetTar(110.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(40.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(85.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().SetTar(1.0);
							}
						}
						// Color 9
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.SetTar(CompactDrawAttributeX(145.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(CompactDrawAttributeY(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.SetTar(145.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().SetTar(1.0);
							}
						}
						// Color 10
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.SetTar(CompactDrawAttributeX(145.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(CompactDrawAttributeY(40.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(CompactDrawAttributeY(85.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.SetTar(145.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(40.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(85.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().SetTar(1.0);
							}
						}
						// Color 11
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.SetTar(CompactDrawAttributeX(180.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(CompactDrawAttributeY(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.SetTar(180.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().SetTar(2.0);
							}
							else
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().SetTar(1.0);
							}
						}
					}
					{ /**/ }
					// 画笔样式区域
					{
						auto SetDrawAttributeSvgColor = [&](BarUISetSvgEnum type, COLORREF color)
							{
								auto& svgColor = svgMap[type]->color1.value();
								// 属性栏隐藏时预先完成设色，再次展开不会出现黑色到主题色的过程。
								if (forNum == 1 || !barState.drawAttribute) svgColor.SetDirect(color);
								else svgColor.SetTar(color);
							};
						// 画笔
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->x.SetTar(CompactDrawAttributeSize(5.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->y.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->w.SetTar(CompactDrawAttributeSize(50.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->h.SetTar(CompactDrawAttributeSize(50.0));
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->x.SetTar(0.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->y.SetTar(CompactDrawAttributeSize(5.0));
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->SetWH(nullopt, CompactDrawAttributeSize(20.0));
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->w.SetTar(CompactDrawAttributeSize(50.0));
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->h.SetTar(CompactDrawAttributeSize(15.0));
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->size.SetTar(CompactDrawAttributeSize(12.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct.SetTar(0.0);

								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->pct.SetTar(0.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->x.SetTar(5.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->y.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->w.SetTar(50.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->h.SetTar(50.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->x.SetTar(0.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->y.SetTar(5.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->SetWH(nullopt, 20.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->w.SetTar(50.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->h.SetTar(15.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->size.SetTar(12.0);

								if (barState.drawAttributeBar.brush1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1) shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct.SetTar(0.1);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct.SetTar(0.0);

								svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->pct.SetTar(1.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->pct.SetTar(1.0);
							}
							if (barStyle.darkStyle)
							{
								if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.SetTar(RGB(88, 255, 236));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Brush1, RGB(88, 255, 236));
								}
								else
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.SetTar(RGB(255, 255, 255));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Brush1, RGB(255, 255, 255));
								}
							}
							else
							{
								if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.SetTar(RGB(88, 255, 236));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Brush1, RGB(88, 255, 236));
								}
								else
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.SetTar(RGB(24, 24, 24));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Brush1, RGB(24, 24, 24));
								}
							}

							if (barState.drawAttributeBar.brush1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1)
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->fill.value().SetTar(RGB(127, 127, 127));
							else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->fill.value().SetTar(RGB(88, 255, 236));
						}
						// 荧光笔
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->x.SetTar(CompactDrawAttributeSize(60.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->y.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->w.SetTar(CompactDrawAttributeSize(50.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->h.SetTar(CompactDrawAttributeSize(50.0));
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->x.SetTar(0.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->y.SetTar(CompactDrawAttributeSize(5.0));
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->SetWH(nullopt, CompactDrawAttributeSize(20.0));
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->w.SetTar(CompactDrawAttributeSize(50.0));
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->h.SetTar(CompactDrawAttributeSize(15.0));
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->size.SetTar(CompactDrawAttributeSize(12.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct.SetTar(0.0);

								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->pct.SetTar(0.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->x.SetTar(60.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->y.SetTar(0.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->w.SetTar(50.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->h.SetTar(50.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->x.SetTar(0.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->y.SetTar(5.0);
								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->SetWH(nullopt, 20.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->w.SetTar(50.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->h.SetTar(15.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->size.SetTar(12.0);

								if (barState.drawAttributeBar.highlight1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1) shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct.SetTar(0.1);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct.SetTar(0.0);

								svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->pct.SetTar(1.0);
								wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->pct.SetTar(1.0);
							}
							if (barStyle.darkStyle)
							{
								if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.SetTar(RGB(88, 255, 236));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Highlight1, RGB(88, 255, 236));
								}
								else
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.SetTar(RGB(255, 255, 255));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Highlight1, RGB(255, 255, 255));
								}
							}
							else
							{
								if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.SetTar(RGB(88, 255, 236));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Highlight1, RGB(88, 255, 236));
								}
								else
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.SetTar(RGB(24, 24, 24));
									SetDrawAttributeSvgColor(BarUISetSvgEnum::DrawAttributeBar_Highlight1, RGB(24, 24, 24));
								}
							}

							if (barState.drawAttributeBar.highlight1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1)
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->fill.value().SetTar(RGB(127, 127, 127));
							else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->fill.value().SetTar(RGB(88, 255, 236));
						}

						// 选中
						{
							if (!barState.drawAttribute)
							{
								if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.SetTar(CompactDrawAttributeSize(60.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.SetTar(CompactDrawAttributeSize(5.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->w.SetTar(CompactDrawAttributeSize(50.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->h.SetTar(CompactDrawAttributeSize(50.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->pct.SetTar(0.0);
							}
							else
							{
								if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.SetTar(60.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.SetTar(5.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->w.SetTar(50.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->h.SetTar(50.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->pct.SetTar(0.2);
							}
							if (barStyle.darkStyle)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->fill.value().SetTar(RGB(88, 255, 236));
							}
							else
							{
								// TODO
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->fill.value().SetTar(RGB(88, 255, 236));
							}
						}
						// 选中滑动槽
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->x.SetTar(CompactDrawAttributeX(215.0));
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.SetTar(CompactDrawAttributeY(5.0));
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.SetTar(CompactDrawAttributeY(50.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->w.SetTar(CompactDrawAttributeSize(115.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->h.SetTar(CompactDrawAttributeSize(65.0));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->x.SetTar(215.0);
								if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.SetTar(5.0);
								else shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.SetTar(50.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->w.SetTar(115.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->h.SetTar(65.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->pct.SetTar(0.15);
							}
						}
					}
					{ /**/ }
					// 粗细调节区域
					{
						if (!barState.drawAttribute)
						{
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->x.SetTar(CompactDrawAttributeX(5.0));
							if (barState.widgetPosition.primaryBar)
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.SetTar(CompactDrawAttributeY(75.0));
							else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.SetTar(CompactDrawAttributeY(5.0));
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->w.SetTar(CompactDrawAttributeSize(205.0));
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->h.SetTar(CompactDrawAttributeSize(40.0));

							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->pct.SetTar(0.0);
						}
						else
						{
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->x.SetTar(5.0);
							if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.SetTar(75.0);
							else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.SetTar(5.0);
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->w.SetTar(205.0);
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->h.SetTar(40.0);

							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->pct.SetTar(0.15);
						}

						if (!barState.drawAttribute)
						{
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->w.SetTar(CompactDrawAttributeSize(80.0));
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->h.SetTar(CompactDrawAttributeSize(30.0));
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->size.SetTar(CompactDrawAttributeSize(15.0));
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->pct.SetTar(0.0);
						}
						else
						{
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->w.SetTar(80.0);
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->h.SetTar(30.0);
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->size.SetTar(15.0);
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->pct.SetTar(1.0);
						}
					}

					// 颜色块在收起状态保留缩小后的相对排布，展开时同时恢复坐标和尺寸。
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
					{
						auto shape = shapeMap[static_cast<BarUISetShapeEnum>(i)];
						double size = barState.drawAttribute ? 30.0 : CompactDrawAttributeSize(30.0);
						shape->w.SetTar(size);
						shape->h.SetTar(size);

						auto svg = svgMap[static_cast<BarUISetSvgEnum>(
							static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1)
							+ i - static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1))];
						double svgSize = barState.drawAttribute ? 15.0 : CompactDrawAttributeSize(15.0);
						svg->w.SetTar(svgSize);
						svg->h.SetTar(svgSize);
					}

					// 展开、收起时，属性栏及全部内部控件共用同一个完成时刻。
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect); i++)
					{
						auto obj = shapeMap[static_cast<BarUISetShapeEnum>(i)];
						if (!obj) continue;
						SyncValueDuration(obj->x);
						SyncValueDuration(obj->y);
						SyncValueDuration(obj->w);
						SyncValueDuration(obj->h);
						if (obj->rw.has_value()) SyncValueDuration(obj->rw.value());
						if (obj->rh.has_value()) SyncValueDuration(obj->rh.value());
						if (obj->ft.has_value()) SyncValueDuration(obj->ft.value());
						SyncPctDuration(obj->pct);
						if (obj->framePct.has_value()) SyncPctDuration(obj->framePct.value());
					}
					for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_Highlight1); i++)
					{
						auto obj = svgMap[static_cast<BarUISetSvgEnum>(i)];
						if (!obj) continue;
						SyncValueDuration(obj->x);
						SyncValueDuration(obj->y);
						SyncValueDuration(obj->w);
						SyncValueDuration(obj->h);
						SyncPctDuration(obj->pct);
					}
					for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
						i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay); i++)
					{
						auto obj = wordMap[static_cast<BarUISetWordEnum>(i)];
						if (!obj) continue;
						SyncValueDuration(obj->x);
						SyncValueDuration(obj->y);
						SyncValueDuration(obj->w);
						SyncValueDuration(obj->h);
						SyncValueDuration(obj->size);
						SyncPctDuration(obj->pct);
					}

					if (drawAttributeVisibilityChange)
					{
						// 每次展开或收起都从当前值重建，所有子控件共用本批次的同一结束时刻。
						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect); i++)
						{
							auto obj = shapeMap[static_cast<BarUISetShapeEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
							if (obj->rw.has_value()) obj->rw.value().SetTar(obj->rw.value().tar, operationDur, nullopt, true, syncedValueCurve);
							if (obj->rh.has_value()) obj->rh.value().SetTar(obj->rh.value().tar, operationDur, nullopt, true, syncedValueCurve);
							if (obj->ft.has_value()) obj->ft.value().SetTar(obj->ft.value().tar, operationDur, nullopt, true, syncedValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
							if (obj->framePct.has_value())
								obj->framePct.value().SetTar(obj->framePct.value().tar, operationDur, nullopt, true, syncedPctCurve);
						}
						for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_Highlight1); i++)
						{
							auto obj = svgMap[static_cast<BarUISetSvgEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
						}
						for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
							i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay); i++)
						{
							auto obj = wordMap[static_cast<BarUISetWordEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->size.SetTar(obj->size.tar, operationDur, nullopt, true, syncedValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
						}
					}

					if (drawAttributeSideSwitch)
					{
						// 上下换边与主栏一致：先收拢到绘制按钮位置并隐藏，再向另一侧展开。
						auto drawAttributeBar = shapeMap[BarUISetShapeEnum::DrawAttributeBar];
						drawAttributeBar->x.SetTar(
							drawAttributeBar->x.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->y.SetTar(
							drawAttributeBar->y.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->w.SetTar(
							drawAttributeBar->w.tar, operationDur, drawAttributeCompactWidth, true,
							drawAttributeKeyframeValueCurve);
						drawAttributeBar->h.SetTar(
							drawAttributeBar->h.tar, operationDur, drawAttributeCompactHeight, true,
							drawAttributeKeyframeValueCurve);
						drawAttributeBar->pct.SetTar(
							drawAttributeBar->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						drawAttributeBar->framePct.value().SetTar(
							drawAttributeBar->framePct.value().tar, operationDur, 0.0, true,
							drawAttributeKeyframePctCurve);

						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect); i++)
						{
							auto obj = shapeMap[static_cast<BarUISetShapeEnum>(i)];
							if (!obj) continue;
							double middleW = max(1.0, CompactDrawAttributeSize(obj->w.tar));
							double middleH = max(1.0, CompactDrawAttributeSize(obj->h.tar));
							bool directChild = i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect11)
								|| i == static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove)
								|| i == static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect);
							double middleX = directChild ? drawAttributeCompactWidth / 2.0 - middleW / 2.0 : 0.0;
							double middleY = directChild ? drawAttributeCompactHeight / 2.0 - middleH / 2.0 : 0.0;
							obj->x.SetTar(obj->x.tar, operationDur, middleX, true, drawAttributeKeyframeValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, middleY, true, drawAttributeKeyframeValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur, middleW, true, drawAttributeKeyframeValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur, middleH, true, drawAttributeKeyframeValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
							if (obj->framePct.has_value())
								obj->framePct.value().SetTar(obj->framePct.value().tar, operationDur, 0.0, true,
									drawAttributeKeyframePctCurve);
						}
						for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_Highlight1); i++)
						{
							auto obj = svgMap[static_cast<BarUISetSvgEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->w.tar)), true, drawAttributeKeyframeValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->h.tar)), true, drawAttributeKeyframeValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						}
						for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
							i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay); i++)
						{
							auto obj = wordMap[static_cast<BarUISetWordEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->w.tar)), true, drawAttributeKeyframeValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->h.tar)), true, drawAttributeKeyframeValueCurve);
							obj->size.SetTar(obj->size.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->size.tar)), true, drawAttributeKeyframeValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						}
					}
				}
			}
		}

	#pragma endregion

	#pragma region 动效UI

		bool needRendering = false;

		auto FinishValue = [](BarUiValueClass& value, double targetValue) -> void
			{
				value.val = targetValue;
				value.startV = targetValue;
				value.progress = 0.0;
				value.dur = 0.0;
				value.hasMiddleV = false;
				value.timelineStartProgress = 0.0;
				value.continueTimelinePhase = false;
			};
		auto FinishColor = [](BarUiColorClass& color, COLORREF targetColor) -> void
			{
				color.val = targetColor;
				color.startColor = targetColor;
				color.progress = 0.0;
				color.dur = 0.0;
				color.timelineStartProgress = 0.0;
				color.continueTimelinePhase = false;
			};
		auto FinishPct = [](BarUiPctClass& pct, double targetPct) -> void
			{
				targetPct = isfinite(targetPct) ? clamp(targetPct, 0.0, 1.0) : 0.0;
				pct.tar = targetPct;
				pct.val = targetPct;
				pct.startV = targetPct;
				pct.progress = 0.0;
				pct.dur = 0.0;
				pct.hasMiddleV = false;
				pct.timelineStartProgress = 0.0;
				pct.continueTimelinePhase = false;
			};
		auto MixColorChannel = [](int start, int target, double progress) -> int
			{
				double val = static_cast<double>(start) + static_cast<double>(target - start) * progress;
				return static_cast<int>(clamp(val, 0.0, 255.0) + 0.5);
			};
		auto MixColor = [&](COLORREF startColor, COLORREF targetColor, double progress) -> COLORREF
			{
				// 颜色按 RGB 三通道共享同一条曲线进度。
				return RGB(
					MixColorChannel(GetRValue(startColor), GetRValue(targetColor), progress),
					MixColorChannel(GetGValue(startColor), GetGValue(targetColor), progress),
					MixColorChannel(GetBValue(startColor), GetBValue(targetColor), progress));
			};
		auto ApplyAnimationCurve = [](BarUiCurveEnum curve, double progress,
			double timelineStartProgress, bool continueTimelinePhase) -> double
			{
				if (!continueTimelinePhase) return BarUiApplyCurve(curve, progress);
				double startProgress = clamp(timelineStartProgress, 0.0, 1.0);
				double absoluteProgress = startProgress + (1.0 - startProgress) * clamp(progress, 0.0, 1.0);
				return BarUiApplyCurveRange(curve, startProgress, absoluteProgress);
			};
		auto ChangeState = [&](BarUiStateClass& state, bool forceReplace) -> void
			{
				needRendering = true;
				state.val = state.tar;
			};
		auto ChangeValue = [&](BarUiValueClass& value, bool forceReplace) -> void
			{
				needRendering = true;
				BarUiValueModeEnum mod = value.mod;
				BarUiCurveEnum curve = value.activeCurve;
				double targetValue = value.tar;
				double startValue = value.startV;
				double duration = value.dur;
				double speedRate = BarUiAnimationSpeedRate;

				// 第一阶段：Linear 和 Variable 共用时间进度；Once 或异常时长仍直接到目标。
				if (forceReplace || mod == BarUiValueModeEnum::Once || !isfinite(duration) || duration <= 0.0
					|| !isfinite(speedRate) || speedRate <= 0.0 || animationDtSeconds <= 0.0)
				{
					FinishValue(value, targetValue);
					return;
				}

				double progress = clamp(static_cast<double>(value.progress) + animationDtSeconds * speedRate / duration, 0.0, 1.0);
				double nextValue = 0.0;
				if (value.hasMiddleV)
				{
					// 关键帧固定在批次绝对时间 0.5，前后两段使用独立曲线。
					double middleValue = value.middleV;
					double phaseStart = value.continueTimelinePhase
						? clamp(static_cast<double>(value.timelineStartProgress), 0.0, 1.0) : 0.0;
					double absoluteProgress = phaseStart + (1.0 - phaseStart) * progress;
					if (absoluteProgress < 0.5)
					{
						double segmentStart = clamp(phaseStart * 2.0, 0.0, 1.0);
						double segmentProgress = clamp(absoluteProgress * 2.0, segmentStart, 1.0);
						double localProgress = BarUiApplyCurveRange(curve, segmentStart, segmentProgress);
						nextValue = startValue + (middleValue - startValue) * localProgress;
					}
					else
					{
						double localProgress = BarUiApplyCurve(
							value.activeMiddleCurve, (absoluteProgress - 0.5) * 2.0);
						nextValue = middleValue + (targetValue - middleValue) * localProgress;
					}
				}
				else nextValue = startValue + (targetValue - startValue) * ApplyAnimationCurve(
					curve, progress, value.timelineStartProgress, value.continueTimelinePhase);
				if (!isfinite(nextValue) || progress >= 1.0)
				{
					FinishValue(value, targetValue);
					return;
				}

				value.val = nextValue;
				value.progress = progress;
			};
		auto ChangeColor = [&](BarUiColorClass& color, bool forceReplace) -> void
			{
				needRendering = true;
				COLORREF targetColor = color.tar;
				COLORREF startColor = color.startColor;
				double duration = color.dur;
				double speedRate = BarUiAnimationSpeedRate;
				if (forceReplace || startColor == targetColor || !isfinite(duration) || duration <= 0.0
					|| !isfinite(speedRate) || speedRate <= 0.0 || animationDtSeconds <= 0.0)
				{
					FinishColor(color, targetColor);
					return;
				}

				double progress = clamp(static_cast<double>(color.progress) + animationDtSeconds * speedRate / duration, 0.0, 1.0);
				double curveProgress = ApplyAnimationCurve(color.activeCurve, progress,
					color.timelineStartProgress, color.continueTimelinePhase);
				COLORREF nextColor = MixColor(startColor, targetColor, clamp(curveProgress, 0.0, 1.0));
				if (progress >= 1.0)
				{
					FinishColor(color, targetColor);
					return;
				}

				color.val = nextColor;
				color.progress = progress;
			};
		auto ChangePct = [&](BarUiPctClass& pct, bool forceReplace) -> void
			{
				needRendering = true;
				constexpr double pctEpsilon = 0.000001;
				double targetPct = pct.tar;
				double startPct = pct.startV;
				double duration = pct.dur;
				double speedRate = BarUiAnimationSpeedRate;
				if (forceReplace || !isfinite(targetPct) || !isfinite(startPct)
					|| (!pct.hasMiddleV && abs(targetPct - startPct) <= pctEpsilon)
					|| !isfinite(duration) || duration <= 0.0 || !isfinite(speedRate) || speedRate <= 0.0 || animationDtSeconds <= 0.0)
				{
					FinishPct(pct, targetPct);
					return;
				}

				double progress = clamp(static_cast<double>(pct.progress) + animationDtSeconds * speedRate / duration, 0.0, 1.0);
				double nextPct = 0.0;
				if (pct.hasMiddleV)
				{
					double middlePct = pct.middleV;
					double phaseStart = pct.continueTimelinePhase
						? clamp(static_cast<double>(pct.timelineStartProgress), 0.0, 1.0) : 0.0;
					double absoluteProgress = phaseStart + (1.0 - phaseStart) * progress;
					if (absoluteProgress < 0.5)
					{
						double segmentStart = clamp(phaseStart * 2.0, 0.0, 1.0);
						double segmentProgress = clamp(absoluteProgress * 2.0, segmentStart, 1.0);
						double localProgress = BarUiApplyCurveRange(
							pct.activeCurve, segmentStart, segmentProgress);
						nextPct = startPct + (middlePct - startPct) * localProgress;
					}
					else
					{
						double localProgress = BarUiApplyCurve(
							pct.activeMiddleCurve, (absoluteProgress - 0.5) * 2.0);
						nextPct = middlePct + (targetPct - middlePct) * localProgress;
					}
				}
				else nextPct = startPct + (targetPct - startPct) * ApplyAnimationCurve(
					pct.activeCurve, progress, pct.timelineStartProgress, pct.continueTimelinePhase);
				nextPct = clamp(nextPct, 0.0, 1.0);
				if (!isfinite(nextPct) || progress >= 1.0)
				{
					FinishPct(pct, targetPct);
					return;
				}

				pct.val = nextPct;
				pct.progress = progress;
			};
		auto ChangeString = [&](BarUiStringClass& stringO, bool forceReplace) -> void
			{
				needRendering = true;
				stringO.ApplyTar();
			};
		// 独立的粗细值也进入统一动画时钟，方便后续直接替换为非线性或回弹曲线。
		if (!drawAttributePenThickness.IsSame()) ChangeValue(drawAttributePenThickness, false);

		for (const auto& [key, val] : shapeMap)
		{
			bool forceReplace = false, change = false;
			if (val->forceReplace) val->forceReplace = false, forceReplace = true;

			if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
			if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
			if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
			if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
			if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
			if (val->rw.has_value() && !val->rw->IsSame()) ChangeValue(val->rw.value(), forceReplace), change = true;
			if (val->rh.has_value() && !val->rh->IsSame()) ChangeValue(val->rh.value(), forceReplace), change = true;
			if (val->ft.has_value() && !val->ft->IsSame()) ChangeValue(val->ft.value(), forceReplace), change = true;
			if (val->fill.has_value() && !val->fill->IsSame()) ChangeColor(val->fill.value(), forceReplace), change = true;
			if (val->frame.has_value() && !val->frame->IsSame()) ChangeColor(val->frame.value(), forceReplace), change = true;
			if (val->framePct.has_value() && !val->framePct->IsSame()) ChangePct(val->framePct.value(), forceReplace), change = true;
			if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
		}
		for (const auto& [key, val] : superellipseMap)
		{
			bool forceReplace = false, change = false;
			if (val->forceReplace) val->forceReplace = false, forceReplace = true;

			if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
			if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
			if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
			if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
			if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
			if (val->n.has_value() && !val->n->IsSame()) ChangeValue(val->n.value(), forceReplace), change = true;
			if (val->ft.has_value() && !val->ft->IsSame()) ChangeValue(val->ft.value(), forceReplace), change = true;
			if (val->fill.has_value() && !val->fill->IsSame()) ChangeColor(val->fill.value(), forceReplace), change = true;
			if (val->frame.has_value() && !val->frame->IsSame()) ChangeColor(val->frame.value(), forceReplace), change = true;
			if (val->framePct.has_value() && !val->framePct->IsSame()) ChangePct(val->framePct.value(), forceReplace), change = true;
			if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
		}
		for (const auto& [key, val] : svgMap)
		{
			bool forceReplace = false, change = false;;
			if (val->forceReplace) val->forceReplace = false, forceReplace = true;

			if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
			if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
			if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
			if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
			if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
			if (!val->svg.IsSame()) ChangeString(val->svg, forceReplace), change = true;
			if (val->color1.has_value() && !val->color1->IsSame()) ChangeColor(val->color1.value(), forceReplace), change = true;
			if (val->color2.has_value() && !val->color2->IsSame()) ChangeColor(val->color2.value(), forceReplace), change = true;
			if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
		}
		for (const auto& [key, val] : wordMap)
		{
			bool forceReplace = false, change = false;;
			if (val->forceReplace) val->forceReplace = false, forceReplace = true;

			if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
			if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
			if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
			if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
			if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
			if (!val->size.IsSame()) ChangeValue(val->size, forceReplace), change = true;
			if (!val->content.IsSame()) ChangeString(val->content, forceReplace), change = true;
			if (!val->color.IsSame()) ChangeColor(val->color, forceReplace), change = true;
			if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
		}

		// 特殊体质：按钮
		for (int id = 0; id < barButtomSet.tot; id++)
		{
			BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
			if (temp == nullptr) continue;

			{
				bool forceReplace = false, change = false;;
				if (temp->buttom.forceReplace) temp->buttom.forceReplace = false, forceReplace = true;

				if (!temp->buttom.enable.IsSame()) ChangeState(temp->buttom.enable, forceReplace), change = true;
				if (!temp->buttom.x.IsSame()) ChangeValue(temp->buttom.x, forceReplace), change = true;
				if (!temp->buttom.y.IsSame()) ChangeValue(temp->buttom.y, forceReplace), change = true;
				if (!temp->buttom.w.IsSame()) ChangeValue(temp->buttom.w, forceReplace), change = true;
				if (!temp->buttom.h.IsSame()) ChangeValue(temp->buttom.h, forceReplace), change = true;
				if (temp->buttom.rw.has_value() && !temp->buttom.rw->IsSame()) ChangeValue(temp->buttom.rw.value(), forceReplace), change = true;
				if (temp->buttom.rh.has_value() && !temp->buttom.rh->IsSame()) ChangeValue(temp->buttom.rh.value(), forceReplace), change = true;
				if (temp->buttom.ft.has_value() && !temp->buttom.ft->IsSame()) ChangeValue(temp->buttom.ft.value(), forceReplace), change = true;
				if (temp->buttom.fill.has_value() && !temp->buttom.fill->IsSame()) ChangeColor(temp->buttom.fill.value(), forceReplace), change = true;
				if (temp->buttom.frame.has_value() && !temp->buttom.frame->IsSame()) ChangeColor(temp->buttom.frame.value(), forceReplace), change = true;
				if (temp->buttom.framePct.has_value() && !temp->buttom.framePct->IsSame()) ChangePct(temp->buttom.framePct.value(), forceReplace), change = true;
				if (!temp->buttom.pct.IsSame()) ChangePct(temp->buttom.pct, forceReplace), change = true;
			}

			{
				bool forceReplace = false, change = false;;
				if (temp->icon.forceReplace) temp->icon.forceReplace = false, forceReplace = true;

				if (!temp->icon.enable.IsSame()) ChangeState(temp->icon.enable, forceReplace), change = true;
				if (!temp->icon.x.IsSame()) ChangeValue(temp->icon.x, forceReplace), change = true;
				if (!temp->icon.y.IsSame()) ChangeValue(temp->icon.y, forceReplace), change = true;
				if (!temp->icon.w.IsSame()) ChangeValue(temp->icon.w, forceReplace), change = true;
				if (!temp->icon.h.IsSame()) ChangeValue(temp->icon.h, forceReplace), change = true;
				if (!temp->icon.svg.IsSame()) ChangeString(temp->icon.svg, forceReplace), change = true;
				if (temp->icon.color1.has_value() && !temp->icon.color1->IsSame()) ChangeColor(temp->icon.color1.value(), forceReplace), change = true;
				if (temp->icon.color2.has_value() && !temp->icon.color2->IsSame()) ChangeColor(temp->icon.color2.value(), forceReplace), change = true;
				if (!temp->icon.pct.IsSame()) ChangePct(temp->icon.pct, forceReplace), change = true;
			}

			{
				bool forceReplace = false, change = false;;
				if (temp->name.forceReplace) temp->name.forceReplace = false, forceReplace = true;

				if (!temp->name.enable.IsSame()) ChangeState(temp->name.enable, forceReplace), change = true;
				if (!temp->name.x.IsSame()) ChangeValue(temp->name.x, forceReplace), change = true;
				if (!temp->name.y.IsSame()) ChangeValue(temp->name.y, forceReplace), change = true;
				if (!temp->name.w.IsSame()) ChangeValue(temp->name.w, forceReplace), change = true;
				if (!temp->name.h.IsSame()) ChangeValue(temp->name.h, forceReplace), change = true;
				if (!temp->name.size.IsSame()) ChangeValue(temp->name.size, forceReplace), change = true;
				if (!temp->name.content.IsSame()) ChangeString(temp->name.content, forceReplace), change = true;
				if (!temp->name.color.IsSame()) ChangeColor(temp->name.color, forceReplace), change = true;
				if (!temp->name.pct.IsSame()) ChangePct(temp->name.pct, forceReplace), change = true;
			}
		}

		// 时间轴与属性值在同一帧末尾推进，避免批次剩余时间和实际动画相差一帧。
		mainBarTimeline.Advance(animationDtSeconds, BarUiAnimationSpeedRate);
		drawAttributeTimeline.Advance(animationDtSeconds, BarUiAnimationSpeedRate);

	#pragma endregion

		bool needRenderOnce = BarAtomic::renderOnceFlag.exchange(false);
		if (needRendering || true == BarAtomic::sustainFlag || true == needRenderOnce)
		{
		#pragma region 渲染UI

			current = RECT(0, 0, 0, 0);
			barDeviceContext->BeginDraw();

			// 清除背景
			{
				D2D1_COLOR_F clearColor = Inkeys::Color::ConvertToD2dColor(RGBA(0, 0, 0, 0));
				barDeviceContext->Clear(&clearColor);

				// TODO 绘制纯白全透明警告用户开启 aero
				auto obj = BarUISetWordEnum::BackgroundWarning;
				spec.Word(barDeviceContext.Get(), *wordMap[obj], wordMap[obj]->Inherit(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
			}

			using enum BarUiInheritEnum;
			{
				// 主栏
				{
					// 提前计算依赖
					{
						auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
						// 使用动画中的实际宽高计算左上角，保证超椭圆与内部 SVG 始终围绕中心缩放。
						mainButton->UpInh(BarUiInheritClass(
							mainButton->x.val - mainButton->w.val / 2.0,
							mainButton->y.val - mainButton->h.val / 2.0));
						shapeMap[BarUISetShapeEnum::MainBar]->Inherit(Center, *mainButton);
						barButtomSet.preset[(int)BarButtomPresetEnum::Draw]->buttom.Inherit(CenterFromTopLeft, *shapeMap[BarUISetShapeEnum::MainBar]);
					}

					// 绘制属性
					{
						auto obj = BarUISetShapeEnum::DrawAttributeBar;
						spec.Shape(barDeviceContext.Get(), *shapeMap[obj], shapeMap[obj]->Inherit(Center, barButtomSet.preset[(int)BarButtomPresetEnum::Draw]->buttom), &current, true);

						// Color 区域
						{
							// Color 1
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect1;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect1;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 2
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect2;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect2;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 3
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect3;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect3;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 4
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect4;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect4;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 5
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect5;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect5;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 6
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect6;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect6;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 7
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect7;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect7;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 8
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect8;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect8;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 9
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect9;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect9;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 10
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect10;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect10;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
							// Color 11
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect11;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect11;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
							}
						}
						// 画笔样式区域
						{
							// 选中滑动槽
							{
								auto obj = BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj], shapeMap[obj]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));
							}
							// 选中
							{
								auto obj = BarUISetShapeEnum::DrawAttributeBar_DrawSelect;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj], shapeMap[obj]->Inherit(Left, *shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]));
							}

							// 画笔
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_Brush1;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(Left, *shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_Brush1;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Top, *shapeMap[obj1]));

								auto obj3 = BarUISetWordEnum::DrawAttributeBar_Brush1;
								spec.Word(barDeviceContext.Get(), *wordMap[obj3], wordMap[obj3]->Inherit(ToBottom, *svgMap[obj2]));
							}
							// 荧光笔
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_Highlight1;
								spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(Left, *shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]));

								auto obj2 = BarUISetSvgEnum::DrawAttributeBar_Highlight1;
								spec.Svg(barDeviceContext.Get(), *svgMap[obj2], svgMap[obj2]->Inherit(Top, *shapeMap[obj1]));

								auto obj3 = BarUISetWordEnum::DrawAttributeBar_Highlight1;
								spec.Word(barDeviceContext.Get(), *wordMap[obj3], wordMap[obj3]->Inherit(ToBottom, *svgMap[obj2]));
							}
						}
						// 粗细调节区域
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect;
							spec.Shape(barDeviceContext.Get(), *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay;
							wordMap[obj2]->Inherit(Right, *shapeMap[obj1]); // 提前计算依赖

							// 自定义绘制：粗细预览
							// 收起后继续按当前透明度绘制，直至与其他属性控件同步淡出。
							if (wordMap[obj2]->pct.val > 0.000001)
							{
								FLOAT penThickness = max(0.0f,
									static_cast<FLOAT>(drawAttributePenThickness.val));

								FLOAT tarZoom = barStyle.zoom;
								double tarX = shapeMap[obj1]->inhX + 5.0;
								double tarY = shapeMap[obj1]->inhY + 5.0;
								double tarEndX = wordMap[obj2]->inhX;
								double tarEndY = shapeMap[obj1]->inhY + shapeMap[obj1]->h.val - 5.0;
								double tarRw = 0.0;
								double tarRh = 0.0;
								if (shapeMap[obj1]->rw.has_value()) tarRw = shapeMap[obj1]->rw.value().val;
								if (shapeMap[obj1]->rh.has_value()) tarRh = shapeMap[obj1]->rh.value().val;

								COLORREF color = wordMap[obj2]->color.val;
								double tarPct = wordMap[obj2]->pct.val;

								float rectX1 = static_cast<FLOAT>(tarX) * tarZoom;
								float rectY1 = static_cast<FLOAT>(tarY) * tarZoom;
								float rectX2 = static_cast<FLOAT>(tarEndX) * tarZoom;
								float rectY2 = static_cast<FLOAT>(tarEndY) * tarZoom;
								if (!isfinite(rectX1)) rectX1 = 0.0f;
								if (!isfinite(rectY1)) rectY1 = 0.0f;
								if (!isfinite(rectX2)) rectX2 = rectX1;
								if (!isfinite(rectY2)) rectY2 = rectY1;

								// 文字和粗细区域独立动画时端点可能交叉，先规范化矩形再创建几何体。
								auto tarRect = D2D1::RectF(
									min(rectX1, rectX2), min(rectY1, rectY2),
									max(rectX1, rectX2), max(rectY1, rectY2));

								// ==== 创建圆角矩形几何 ====
								ComPtr<ID2D1Factory> factory;
								barDeviceContext->GetFactory(&factory);

								ComPtr<ID2D1RoundedRectangleGeometry> roundedRectGeo;
								D2D1_ROUNDED_RECT roundedRect = {
									tarRect,
									static_cast<FLOAT>(tarRw) * tarZoom,
									static_cast<FLOAT>(tarRh) * tarZoom
								};
								factory->CreateRoundedRectangleGeometry(roundedRect, &roundedRectGeo);

								// ==== 创建 Layer ====
								ComPtr<ID2D1Layer> layer;
								barDeviceContext->CreateLayer(&layer);

								// ==== 启用裁切层 ====
								D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters();
								layerParams.geometricMask = roundedRectGeo.Get();
								layerParams.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;

								barDeviceContext->PushLayer(&layerParams, layer.Get());

								// ====== 四个经过点（百分比） ======
								auto w = tarRect.right - tarRect.left;
								auto h = tarRect.bottom - tarRect.top;
								float middleX = tarRect.left + w * 0.5f;
								float middleY = tarRect.top + h * 0.5f;
								float insetX = min(5.0f * tarZoom, max(0.0f, w * 0.5f));
								float insetY = min(5.0f * tarZoom, max(0.0f, h * 0.5f));
								auto SafeClamp = [](float value, float bound1, float bound2)
									{
										if (!isfinite(value)) value = 0.0f;
										if (!isfinite(bound1)) bound1 = value;
										if (!isfinite(bound2)) bound2 = value;
										float lower = min(bound1, bound2);
										float upper = max(bound1, bound2);
										return min(max(value, lower), upper);
									};

								// 不再直接调用 std::clamp，确保异常动画输入也不会触发调试断言。
								D2D1_POINT_2F p1 = {
									SafeClamp(tarRect.left + penThickness / 2.0f, tarRect.left + insetX, middleX), middleY };
								D2D1_POINT_2F p4 = {
									SafeClamp(tarRect.right - penThickness / 2.0f, middleX, tarRect.right - insetX), middleY };
								D2D1_POINT_2F p2 = {
									tarRect.left + (p4.x - p1.x) / 3.0f,
									SafeClamp(tarRect.top + penThickness / 2.0f, tarRect.top + insetY, middleY) };
								D2D1_POINT_2F p3 = {
									tarRect.left + (p4.x - p1.x) * 2.0f / 3.0f,
									SafeClamp(tarRect.bottom - penThickness / 2.0f, middleY, tarRect.bottom - insetY) };

								vector<D2D1_POINT_2F> pts = { p1,p2,p3,p4 };

								// ====== 内部 lambda：Catmull-Rom 样条到 Bezier 转换 ======
								auto catmullRomToBeziers = [](const vector<D2D1_POINT_2F>& pts, float tension = 1.0f)
									{
										vector<D2D1_BEZIER_SEGMENT> beziers;
										if (pts.size() < 2) return beziers;

										// 为首尾补点（非闭合）
										vector<D2D1_POINT_2F> p;
										p.push_back(pts.front());
										p.insert(p.end(), pts.begin(), pts.end());
										p.push_back(pts.back());

										for (int i = 1; i < (int)p.size() - 2; i++)
										{
											D2D1_POINT_2F p0 = p[i - 1];
											D2D1_POINT_2F p1 = p[i];
											D2D1_POINT_2F p2 = p[i + 1];
											D2D1_POINT_2F p3 = p[i + 2];

											D2D1_BEZIER_SEGMENT seg;
											seg.point1 = {
												p1.x + (p2.x - p0.x) / 6.0f * tension,
												p1.y + (p2.y - p0.y) / 6.0f * tension
											};
											seg.point2 = {
												p2.x - (p3.x - p1.x) / 6.0f * tension,
												p2.y - (p3.y - p1.y) / 6.0f * tension
											};
											seg.point3 = p2;
											beziers.push_back(seg);
										}
										return beziers;
									};

								// 生成 Bezier 段
								auto beziers = catmullRomToBeziers(pts, 1.0f);

								// ====== 创建 PathGeometry ======
								ComPtr<ID2D1PathGeometry> pathGeometry;
								factory->CreatePathGeometry(&pathGeometry);

								ComPtr<ID2D1GeometrySink> sink;
								pathGeometry->Open(&sink);

								sink->BeginFigure(pts.front(), D2D1_FIGURE_BEGIN_HOLLOW);
								for (auto& bz : beziers) sink->AddBezier(bz);
								sink->EndFigure(D2D1_FIGURE_END_OPEN);

								sink->Close();

								// ==== 画刷 ====
								ComPtr<ID2D1SolidColorBrush> brush;
								barDeviceContext->CreateSolidColorBrush(
									Inkeys::Color::ConvertToD2dColor(color, tarPct),
									&brush
								);

								// ==== Stroke Style（圆头、圆角）====
								ComPtr<ID2D1StrokeStyle> strokeStyle;
								D2D1_STROKE_STYLE_PROPERTIES props{};
								props.startCap = D2D1_CAP_STYLE_ROUND;
								props.endCap = D2D1_CAP_STYLE_ROUND;
								props.lineJoin = D2D1_LINE_JOIN_ROUND;
								factory->CreateStrokeStyle(&props, nullptr, 0, &strokeStyle);

								// ==== 绘制贝塞尔曲线（裁切生效）====
								barDeviceContext->DrawGeometry(pathGeometry.Get(), brush.Get(), penThickness, strokeStyle.Get());

								// ==== 结束裁切 ====
								barDeviceContext->PopLayer();
							}

							// 数字直接取曲线的同一个动画值，保证切换画笔类型时同步连续变化。
							int displayedThickness = static_cast<int>(lround(clamp(
								static_cast<double>(drawAttributePenThickness.val), 0.0, 999.0)));
							wstring thicknessText = L"粗细" + format(L" {:>3}", displayedThickness);
							wordMap[obj2]->content.SetVal(thicknessText);
							wordMap[obj2]->content.SetTar(thicknessText);

							// obj2
							spec.Word(barDeviceContext.Get(), *wordMap[obj2], wordMap[obj2]->Inherit(Right, *shapeMap[obj1]), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
						}
					}

					// 主栏
					auto obj = BarUISetShapeEnum::MainBar;
					spec.Shape(barDeviceContext.Get(), *shapeMap[obj], BarUiInheritClass(shapeMap[obj]->inhX, shapeMap[obj]->inhY), &current, true);

					// 主栏按钮
					for (int id = 0; id < barButtomSet.tot; id++)
					{
						BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
						if (temp == nullptr) continue;

						spec.Shape(barDeviceContext.Get(), temp->buttom, temp->buttom.Inherit(CenterFromTopLeft, *shapeMap[BarUISetShapeEnum::MainBar]));
						spec.Svg(barDeviceContext.Get(), temp->icon, temp->icon.Inherit(Center, temp->buttom));
						spec.Word(barDeviceContext.Get(), temp->name, temp->name.Inherit(Center, temp->buttom));
					}
				}
				{ /**/ }

				// 主按钮
				{
					auto obj = BarUISetSuperellipseEnum::MainButton;
					spec.Superellipse(barDeviceContext.Get(), *superellipseMap[obj], BarUiInheritClass(superellipseMap[obj]->inhX, superellipseMap[obj]->inhY), &current, true);

					{
						auto obj = BarUISetSvgEnum::logo1;
							spec.Svg(barDeviceContext.Get(), *svgMap[obj], svgMap[obj]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]));
						}
					}

					// 动画中的子控件可能暂时超出父级边界，脏区必须包含其真实新旧范围以清除残影。
					double dirtyZoom = barStyle.zoom;
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect); i++)
					{
						auto obj = shapeMap[static_cast<BarUISetShapeEnum>(i)];
						if (obj) BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(*obj, dirtyZoom));
					}
					for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_Highlight1); i++)
					{
						auto obj = svgMap[static_cast<BarUISetSvgEnum>(i)];
						if (obj) BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(*obj, dirtyZoom));
					}
					for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
						i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay); i++)
					{
						auto obj = wordMap[static_cast<BarUISetWordEnum>(i)];
						if (obj) BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(*obj, dirtyZoom));
					}
					for (int id = 0; id < barButtomSet.tot; id++)
					{
						BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
						if (!temp) continue;
						BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(temp->buttom, dirtyZoom));
						BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(temp->icon, dirtyZoom));
						BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(temp->name, dirtyZoom));
					}
				}
			{ /**/ }

			// 调试 + FPS
			/*
			{
				double tarZoom = barStyle.zoom;
				wstring content = L"开发版本 " + editionDate + L" | 不代表最终品质 | " + fps;

				ComPtr<IDWriteTextFormat> pTextFormat;
				pTextFormat = barMedia.formatCache->GetFormat(
					L"HarmonyOS Sans SC",
					12.0 * tarZoom,
					dWriteFontCollection,
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					L"zh-cn",
					DWRITE_TEXT_ALIGNMENT_LEADING, // 指定文本左对齐
					DWRITE_PARAGRAPH_ALIGNMENT_NEAR // 指定段落顶部对齐
				);

				// 3. 创建画刷
				ComPtr<ID2D1SolidColorBrush> pBrush;
				barDeviceContext->CreateSolidColorBrush(
					D2D1::ColorF(255, 255, 255, 0.5),
					&pBrush);

				double tarX = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->inhX;
				double tarY = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->inhY + barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetH();

				// 4. 设定绘制区域
				D2D1_RECT_F layoutRect = D2D1::RectF(tarX * tarZoom, tarY * tarZoom, (tarX + 300) * tarZoom, (tarY + 20) * tarZoom);

				RECT tmp = RECT((LONG)(layoutRect.left), (LONG)(layoutRect.top), (LONG)(layoutRect.right), (LONG)(layoutRect.bottom));
				BarRenderingAttribute::UnionRectInPlace(current, tmp);

				// 5. 绘制文本
				barDeviceContext->DrawTextW(
					content.c_str(),           // text
					(UINT32)content.length(),  // text length
					pTextFormat,               // format
					layoutRect,                // layout rect
					pBrush,                    // brush
					D2D1_DRAW_TEXT_OPTIONS_NONE
				);
			}
			*/

			// 如果你需要测试脏区更新的区域，则可以取消注释下面的代码，并注释下方的脏区更新代码
			/*
			RECT target = original;
			original = current;
			BarRenderingAttribute::UnionRectInPlace(target, current);
			{
				// 脏区更新限制
				if (target.left < 0) target.left = 0;
				if (target.top < 0) target.top = 0;
				if (target.right > barWindow.w) target.right = barWindow.w;
				if (target.bottom > barWindow.h) target.bottom = barWindow.h;
			}

			{
				COLORREF frame = RGB(255, 0, 0);
				D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(target.left, target.top, target.right - 1, target.bottom - 1), 0, 0);

				ComPtr<ID2D1SolidColorBrush> spBorderBrush;
				barDeviceContext->CreateSolidColorBrush(Inkeys::Color::ConvertToD2dColor(frame, 1.0), &spBorderBrush);

				barDeviceContext->DrawRoundedRectangle(&roundedRect, spBorderBrush.Get(), 1.0f);
			}
			*/

			barDeviceContext->Flush();

			{
				// 脏区更新
				RECT target = original;
				original = current;
				BarRenderingAttribute::UnionRectInPlace(target, current);
				{
					// 脏区更新限制
					if (target.left < 0) target.left = 0;
					if (target.top < 0) target.top = 0;
					if (target.right > barWindow.w) target.right = barWindow.w;
					if (target.bottom > barWindow.h) target.bottom = barWindow.h;
				}

				// psize 指定窗口本次更新“新内容”宽高
				// pptDst 指定新内容贴到屏幕上的位置（左上角）
				// pptSrc 从源内存 DC 的哪个位置起贴内容

				// 设置窗口位置
				POINT ptDst = { 0, 0 };
				if (!barGdiInterop)
				{
					if (IDTLogger) IDTLogger->error("[BarUISetClass::Rendering] barGdiInterop 为空，跳过 GetDC");
				}
				else
				{
					// 获取 DC
					HDC hdc = nullptr;
					HRESULT hr = barGdiInterop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &hdc);
					if (FAILED(hr))
					{
						if (IDTLogger) IDTLogger->error("[BarUISetClass::Rendering] GetDC 失败, hr=0x{:08X}", static_cast<unsigned int>(hr));
					}
					else
					{
						ulwi.pptDst = &ptDst;
						ulwi.hdcSrc = hdc;
						ulwi.prcDirty = &target;
						UpdateLayeredWindowIndirect(floating_window, &ulwi);

						barGdiInterop->ReleaseDC(nullptr);
					}
				}
			}

			barDeviceContext->EndDraw();
			barMedia.formatCache->Clean();

		#pragma endregion
		}
		else
		{
			BarAtomic::wait.WaitFalse();
			BarAtomic::wait.Store(false);
		}

		if (forNum == 1)
		{
			IdtWindowsIsVisible.floatingWindow = true;
		}
		// 帧率锁
		{
			HighPrecisionWait(chrono::duration<double, milli>(chrono::high_resolution_clock::now() - reckon).count(), 60.0);

			//double delay = 1000.0 / 60.0 - chrono::duration<double, milli>(chrono::high_resolution_clock::now() - reckon).count();
			//if (delay >= 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
		}

		{
			double cost = chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			fps = format(L"{:.2f} FPS", 1000.0 / cost);
		}
		reckon = chrono::high_resolution_clock::now();
	}

	return;
}
// 鼠标交互
void BarUISetClass::Interact()
{
	ExMessage msg;
	BarButtomClass* lastClickedMainBarButton = nullptr;
	while (!offSignal)
	{
		hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);

		{
			bool continueFlag = true;

			// 主按钮
			if (auto obj = superellipseMap[BarUISetSuperellipseEnum::MainButton]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
			{
				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					double moveDis = Seek(msg);
					if (moveDis <= 20)
					{
						mainButtonClickPulseSerial.fetch_add(1, std::memory_order_relaxed);
						// 展开/收起主栏
						if (barState.fold) barState.fold = false;
						else barState.fold = true;
						UpdateRendering();
					}

					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}
				if (msg.message == WM_RBUTTONDOWN && setlist.RightClickClose)
				{
					if (MessageBox(floating_window, L"Whether to turn off 智绘教Inkeys?\n是否关闭 智绘教Inkeys？", L"Inkeys Tips | 智绘教提示", MB_OKCANCEL | MB_SYSTEMMODAL) == 1) CloseProgram();

					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}
			}

			// 按钮
			if (continueFlag)
			{
				// 特殊体质：按钮
				for (int id = 0; id < barButtomSet.tot; id++)
				{
					BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
					if (temp == nullptr || temp->hide) continue;

					// 双击第二击仍归属于第一击按钮，避免动画中按钮位移导致命中丢失。
					bool doubleClickContinuation = msg.message == WM_LBUTTONDBLCLK
						&& temp == lastClickedMainBarButton;
					if (temp->buttom.IsClick(msg.x, msg.y, barStyle.zoom) || doubleClickContinuation)
					{
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK)
						{
							bool clickCompleted = false;
							temp->state->emph = BarWidgetEmphasize::Pressed; UpdateRendering(false);
							while (true)
							{
								hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);
								if (doubleClickContinuation || temp->buttom.IsClick(msg.x, msg.y, barStyle.zoom))
								{
									if (!msg.lbutton)
									{
										if (temp->clickFunc) temp->clickFunc();
										lastClickedMainBarButton = temp;
										clickCompleted = true;
										UpdateRendering();

										break;
									}
								}
								else break;
							}
							temp->state->emph = BarWidgetEmphasize::None; UpdateRendering(false);

							// 成功点击后保留队列中的下一击；拖出取消时仍清理本轮残留消息。
							if (!clickCompleted) hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
						break;
					}
				}
			}

			// 绘制属性
			{
				// 颜色选择
				if (continueFlag)
				{
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1); i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
					{
						auto enumValue = static_cast<BarUISetShapeEnum>(i);

						if (auto obj = shapeMap[enumValue]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
						{
							continueFlag = false;
							if (msg.lbutton)
							{
								SetPenColor(Inkeys::Color::SetAlphaR(obj->fill.value().tar, 255));
								UpdateRendering();

								while (true)
								{
									hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);

									if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
									{
										if (!msg.lbutton) break;
									}
									else break;
								}

								hiex::flushmessage_win32(EM_MOUSE, floating_window);
							}
						}

						if (!continueFlag) break;
					}
				}

				// 画笔
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.drawAttributeBar.brush1Press = true; UpdateRendering(false);
						while (true)
						{
							hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
								if (!msg.lbutton)
								{
									stateMode.Pen.ModeSelect = PenModeSelectEnum::IdtPenBrush1;
									barButtomSet.UpdateDrawButtonStyle();
									UpdateRendering();

									break;
								}
							}
							else break;
						}
						barState.drawAttributeBar.brush1Press = false; UpdateRendering(false);

						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
				// 荧光笔
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.drawAttributeBar.highlight1Press = true; UpdateRendering(false);
						while (true)
						{
							hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
								if (!msg.lbutton)
								{
									stateMode.Pen.ModeSelect = PenModeSelectEnum::IdtPenHighlighter1;
									barButtomSet.UpdateDrawButtonStyle();
									UpdateRendering();

									break;
								}
							}
							else break;
						}
						barState.drawAttributeBar.highlight1Press = false; UpdateRendering(false);

						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
			}
		}
	}
}
// 渲染更新：状态更新 + 通知计算并渲染
void BarUISetClass::UpdateRendering(bool updateState)
{
	static mutex mtx;
	lock_guard<mutex> lock(mtx);

	// 状态更新
	if (updateState)
	{
		barButtomSet.StateUpdate();
		// 非画笔模式的 GetPenWidth 为 0，收起过程中保留最后一次有效的粗细文字。
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
			barState.ThicknessDisplayUpdate();
	}

	// 通知计算并渲染
	BarAtomic::wait.Store(true);
}
// 拖动交互
double BarUISetClass::Seek(const ExMessage& msg)
{
	auto IsLeftButtonDown = []() -> bool
		{
			return Inkeys::Inputs::IsKeyBoardDown(VK_LBUTTON) || ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		};
	if (!IsLeftButtonDown()) return 0;

	auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
	if (!mainButton) return 0;

	POINT p;
	GetCursorPos(&p);

	double firX = static_cast<double>(p.x);
	double firY = static_cast<double>(p.y);

	double ret = 0.0;

	BarAtomic::sustainFlag = true;
	UpdateRendering();

	double tarZoom = barStyle.zoom;
	while (1)
	{
		if (!IsLeftButtonDown()) break;
		GetCursorPos(&p);

		if (firX == p.x && firY == p.y)
		{
			this_thread::sleep_for(chrono::milliseconds(15));
			continue;
		}

		double nextX = mainButton->x.tar + static_cast<double>(p.x - firX) / tarZoom;
		double nextY = mainButton->y.tar + static_cast<double>(p.y - firY) / tarZoom;

		// 临时限制主按钮整体始终留在主屏幕内，先不处理贴边隐藏和多显示器。
		double frameHalf = 0.0;
		if (mainButton->ft.has_value()) frameHalf = max(0.0, mainButton->ft.value().tar / 2.0);

		double minX = mainButton->GetW() / 2.0 + frameHalf;
		double minY = mainButton->GetH() / 2.0 + frameHalf;
		double maxX = static_cast<double>(barWindow.w) / tarZoom - mainButton->GetW() / 2.0 - frameHalf;
		double maxY = static_cast<double>(barWindow.h) / tarZoom - mainButton->GetH() / 2.0 - frameHalf;

		if (maxX < minX) maxX = minX;
		if (maxY < minY) maxY = minY;

		mainButton->x.SetDirect(clamp(nextX, minX, maxX));
		mainButton->y.SetDirect(clamp(nextY, minY, maxY));

		ret += sqrt((p.x - firX) * (p.x - firX) + (p.y - firY) * (p.y - firY));
		firX = static_cast<double>(p.x), firY = static_cast<double>(p.y);
		// 拖动时收起主栏
		if (setlist.regularSetting.moveRecover)
		{
			if (ret > 20 && barState.fold == false)
			{
				barState.fold = true;
			}
		}
	}
	// 左右侧只在松手时提交；若动画尚未结束，新提交会从当前 val 重建关键帧过程。
	bool previousMainBarSide = barState.widgetPosition.mainBar;
	barState.PositionUpdate(tarZoom);
	if (previousMainBarSide != barState.widgetPosition.mainBar) UpdateRendering(false);

	BarAtomic::sustainFlag = false;
	return ret;
}

// 全局 Bar UI 集合
BarUISetClass barUISet;

// ====================
// 环境

// 初始化

namespace Inkeys::UI::Bar
{
	void Initialization()
	{
		Inkeys::Thread::StatusGuard guard("BarInitializationClass::BarInitialization");

		// 初始化
		InitializeWindow(barUISet);
		InitializeMedia(barUISet);
		InitializeUI(barUISet);

		barUISet.barMedia.LoadFormat();

		// 初始化 按钮 们
		barUISet.barButtomSet.PresetInitialization();
		{
			barUISet.barButtomSet.Load();
			barUISet.barButtomSet.StateUpdate();
		}

		barUISet.barState.PositionUpdate(barUISet.barStyle.zoom);

		// 线程
		thread(FloatingInstallHook).detach();
		thread([&]() { barUISet.Rendering(); }).detach();
		thread([&]() { barUISet.Interact(); }).detach();

		// 等待

		while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

		// 反初始化

		unsigned int waitTimes = 1;
		for (; waitTimes <= 10; waitTimes++)
		{
			using namespace Inkeys::Thread;

			if (!GetStatus("BarUISetClass::Rendering")) break;
			this_thread::sleep_for(chrono::milliseconds(500));
		}

		return;
	}

	void InitializeWindow(BarUISetClass& barUISet)
	{
		DisableResizing(floating_window, true); // hiex 禁止窗口拉伸

		SetWindowLong(floating_window, GWL_STYLE, GetWindowLong(floating_window, GWL_STYLE) & ~WS_CAPTION); // 隐藏窗口标题栏
		SetWindowLong(floating_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW); // 隐藏窗口任务栏图标

		barUISet.barWindow.x = 0;
		barUISet.barWindow.y = 0;
		barUISet.barWindow.w = MainMonitor.MonitorWidth;
		barUISet.barWindow.h = MainMonitor.MonitorHeight - 1;
		barUISet.barWindow.pct = 255;
		SetWindowPos(floating_window, NULL, barUISet.barWindow.x, barUISet.barWindow.y, barUISet.barWindow.w, barUISet.barWindow.h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_DRAWFRAME); // 设置窗口位置尺寸

		// 设置自定义窗口消息回调
		hiex::SetWndProcFunc(floating_window, barWindowMsgCallback);
	}
	void InitializeMedia(BarUISetClass& barUISet)
	{
		barUISet.barMedia.LoadExImage();
	}
	void InitializeUI(BarUISetClass& barUISet)
	{
		Inkeys::UI::Bar::Zoom::Initialize(barUISet);

		// 定义主按钮的位置（Inkeys2 兼容模式）
		double mainX, mainY;
		{
			mainX = static_cast<double>(barUISet.barWindow.x + barUISet.barWindow.w - 80 - 50) / barUISet.barStyle.zoom;
			mainY = static_cast<double>(barUISet.barWindow.y + barUISet.barWindow.h - 80 - 200) / barUISet.barStyle.zoom;
		}

		// 定义 UI 控件
		{
			// 背景层
			{
				auto word = make_shared<BarUiWordClass>(700.0, 150.0, 1200.0, 300.0, L"", 30.0, RGB(255, 255, 255));
				word->content.Initialization(L"软件遇到透明背景无法正常显示的故障\n\nexe属性->关闭使用简化的颜色模式\nWindows7用户请开启Aero主题\n\n联系开发者->软件选项主页中\n重启软件试试");
				word->pct.Initialization(0.0);
				word->enable.Initialization(true);
				barUISet.wordMap[BarUISetWordEnum::BackgroundWarning] = word;
			}

			// 主按钮
			{
				auto superellipse = make_shared<BarUiSuperellipseClass>(mainX, mainY, 80.0, 80.0, 3.0, 1.0, RGB(24, 24, 24), RGB(255, 255, 255));
				superellipse->pct.Initialization(0.6);
				superellipse->framePct = BarUiPctClass(0.18);
				superellipse->enable.Initialization(true);
				barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton] = superellipse;

				{
					auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
					svg->InitializationFromResource(L"UI", L"logo1");
					svg->SetWH(nullopt, 80.0);
					svg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::logo1] = svg;
				}
				{
					// TODO “收起” 文字标识
				}
			}
			// 主栏
			{
				auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 80.0, 80.0, 8.0, 8.0, 1.0, RGB(24, 24, 24), RGB(255, 255, 255));
				shape->pct.Initialization(0.8);
				shape->framePct = BarUiPctClass(0.18);
				shape->w.mod = BarUiValueModeEnum::Variable;
				shape->h.mod = BarUiValueModeEnum::Variable;
				shape->enable.Initialization(true);
				barUISet.shapeMap[BarUISetShapeEnum::MainBar] = shape;

				// 绘制属性（一级菜单）
				{
					auto shape = make_shared<BarUiShapeClass>(10.0, 10.0, 60.0, 60.0, 8.0, 8.0, 1.0, RGB(24, 24, 24), RGB(255, 255, 255));
					shape->pct.Initialization(0.8);
					shape->framePct = BarUiPctClass(0.18);
					shape->w.mod = BarUiValueModeEnum::Variable;
					shape->h.mod = BarUiValueModeEnum::Variable;
					shape->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar] = shape;

					// Color 区域
					{
						// Color 1
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(255, 255, 255), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1] = svg;
						}
						// Color 2
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(0, 0, 0), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2] = svg;
						}
						// Color 3
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(255, 139, 0), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3] = svg;
						}
						// Color 4
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(50, 30, 181), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4] = svg;
						}
						// Color 5
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(255, 197, 16), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5] = svg;
						}
						// Color 6
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(255, 16, 0), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6] = svg;
						}
						// Color 7
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(78, 161, 183), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7] = svg;
						}
						// Color 8
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(50, 110, 217), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8] = svg;
						}
						// Color 9
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(102, 213, 82), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9] = svg;
						}
						// Color 10
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(48, 108, 0), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10] = svg;
						}
						// Color 11
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, RGB(255, 30, 207), RGB(127, 127, 127));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11] = svg;
						}
					}
					{ /**/ }
					// 画笔样式区域
					{
						// 画笔
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 50.0, 50.0, 4.0, 4.0, 1.0, RGB(0, 0, 0), nullopt);
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, RGB(0, 0, 0), nullopt);
							svg->InitializationFromResource(L"UI", L"barBrush1");
							svg->SetWH(nullopt, 20.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1] = svg;

							auto word = make_shared<BarUiWordClass>(0.0, 5.0, 50.0, 15.0, L"画笔", 12.0, RGB(255, 255, 255));
							word->enable.Initialization(true);
							barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1] = word;
						}
						// 荧光笔
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 50.0, 50.0, 4.0, 4.0, 1.0, RGB(0, 0, 0), nullopt);
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, RGB(0, 0, 0), nullopt);
							svg->InitializationFromResource(L"UI", L"barHighlighter1");
							svg->SetWH(nullopt, 20.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1] = svg;

							auto word = make_shared<BarUiWordClass>(0.0, 5.0, 50.0, 15.0, L"荧光笔", 12.0, RGB(255, 255, 255));
							word->enable.Initialization(true);
							barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1] = word;
						}

						// 选中
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 50.0, 50.0, 4.0, 4.0, 1.0, RGB(0, 0, 0), nullopt);
							shape->x.curve = BarUiCurveEnum::EaseOutBack; // 仅画笔类型的背景指示横移使用回弹
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect] = shape;
						}
						// 选中滑动槽
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 60.0, 60.0, 4.0, 4.0, 1.0, RGB(127, 127, 127), nullopt);
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove] = shape;
						}
					}
					// 粗细调节区域
					{
						auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 60.0, 60.0, 4.0, 4.0, 1.0, RGB(127, 127, 127), nullopt);
						shape->enable.Initialization(true);
						barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect] = shape;

						auto word = make_shared<BarUiWordClass>(-10.0, 0.0, 30.0, 30.0, L"", 15.0, RGB(255, 255, 255));
						word->enable.Initialization(true);
						barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay] = word;
					}
				}
			}
		}
	}
}
