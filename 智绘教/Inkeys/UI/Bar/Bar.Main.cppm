module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtD2DPreparation.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtState.h"
#include "../../../IdtWindow.h"
#include "../../../IdtText.h"
#include "../../Conv/IdtColor.h"
#include "../../Other/IdtInputs.h"

// ====================
// 临时
extern IdtAtomic<bool> ConfirmaNoMouMsgSignal, ConfirmaNoMouFunSignal;
void FloatingInstallHook();

export module Inkeys.UI.Bar.Main;

import Inkeys.UI.Bar.UI;
import Inkeys.UI.Bar.State;
import Inkeys.UI.Bar.Bottom;
import Inkeys.UI.Bar.Format;
import Inkeys.UI.Bar.RenderingAttribute;

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
		static DWORD activeTouchId = 0;   // 0表示无活动ID
		static bool isTouchActive = false;

		UINT cInputs = LOWORD(wParam);
		TOUCHINPUT inputs[32];
		if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, inputs, sizeof(TOUCHINPUT)))
		{
			bool touchIdCheck = false; // 检测当前活动ID是否还存在
			short x = 0, y = 0; // 坐标

			for (UINT i = 0; i < cInputs; i++)
			{
				const TOUCHINPUT& ti = inputs[i];

				double xO = static_cast<double>(ti.x) / 100.0;
				double yO = static_cast<double>(ti.y) / 100.0;
				x = static_cast<short>(xO + 0.5);
				y = static_cast<short>(yO + 0.5);

				if (ti.dwFlags & TOUCHEVENTF_DOWN)
				{
					// 如果当前无activeID，则锁定第一个DOWN点
					if (!isTouchActive)
					{
						activeTouchId = ti.dwID;
						isTouchActive = true;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONDOWN;
							msgMouse.x = x;
							msgMouse.y = y;
							msgMouse.lbutton = true;

							int index = hiex::GetWindowIndex(hWnd, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}
				if (ti.dwFlags & TOUCHEVENTF_MOVE)
				{
					if (isTouchActive && ti.dwID == activeTouchId)
					{
						ExMessage msgMouse = {};
						msgMouse.message = WM_MOUSEMOVE;
						msgMouse.x = x;
						msgMouse.y = y;
						msgMouse.lbutton = true;

						int index = hiex::GetWindowIndex(hWnd, false);
						unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
						hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
						lg_vecWindows_vecMessage_sm.unlock();
					}
				}
				if (ti.dwFlags & TOUCHEVENTF_UP)
				{
					if (isTouchActive && ti.dwID == activeTouchId)
					{
						activeTouchId = 0;
						isTouchActive = false;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONUP;
							msgMouse.x = x;
							msgMouse.y = y;
							msgMouse.lbutton = false;

							int index = hiex::GetWindowIndex(hWnd, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}

				if (isTouchActive && ti.dwID == activeTouchId) touchIdCheck = true;
			}

			if (isTouchActive && !touchIdCheck)
			{
				activeTouchId = 0;
				isTouchActive = false;

				{
					ExMessage msgMouse = {};
					msgMouse.message = WM_LBUTTONUP;
					msgMouse.x = x;
					msgMouse.y = y;
					msgMouse.lbutton = false;

					int index = hiex::GetWindowIndex(hWnd, false);
					unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
					hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
					lg_vecWindows_vecMessage_sm.unlock();
				}
			}

			CloseTouchInputHandle((HTOUCHINPUT)lParam);
		}

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

		// 否则当成真正的鼠标消息处理
		// 您的鼠标处理逻辑
		break;
	}

	default:
		return HIWINDOW_DEFAULT_PROC;
	}

	return HIWINDOW_DEFAULT_PROC;
}

// 窗口模态信息
export class BarWindowPosClass
{
public:
	IdtAtomic<int> x = 0, y = 0;
	IdtAtomic<unsigned int> w = 0, h = 0;
	IdtAtomic<unsigned int> pct = 255; // 透明度
};

// ====================
// 媒体

// 媒体操控类
export class BarMediaClass
{
public:
	void LoadExImage()
	{
	}
	void LoadFormat()
	{
		formatCache = make_unique<BarFormatCache>(dWriteFactory1);
	}

public:
	enum class BarExImageEnum : int
	{
	};

	// 似乎被废弃了，新版较多地使用的是 svg

public:
	IMAGE Image[10];
	unique_ptr<BarFormatCache> formatCache;

protected:
};

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

// 前向声明
class BarUISetClass;
// 前向声明

// 控件枚举
export enum class BarUISetShapeEnum : int
{
	MainBar,

	DrawAttributeBar,
	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,
	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_DrawSelect,
	DrawAttributeBar_DrawSelectGroove,
	DrawAttributeBar_ThicknessSelect,
};
export enum class BarUISetSuperellipseEnum : int
{
	MainButton,
};
export enum class BarUISetSvgEnum : int
{
	logo1,

	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
};
export enum class BarUISetWordEnum : int
{
	BackgroundWarning,

	MainButton,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_ThicknessDisplay,
};

// 具体渲染
export class BarUIRendering
{
public:
	BarUIRendering() {};
	BarUIRendering(BarUISetClass* barUISetClassT);

public:
	bool Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Svg(ID2D1DeviceContext* deviceContext, BarUiSVGClass& svg, const BarUiInheritClass& inh);
	bool Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER);

public:
	BarUISetClass* barUISetClass = nullptr;
};

// UI 总集
export class BarUISetClass
{
public:
	BarUISetClass() : spec(this) {};

	// 渲染
	void Rendering()
	{
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
		CComPtr<ID2D1DeviceContext>				barDeviceContext;
		CComPtr<ID2D1Bitmap1>					barBackgroundBitmap;
		CComPtr<ID2D1GdiInteropRenderTarget>	barGdiInterop;
		{
			d2dDevice_WARP->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &barDeviceContext);

			D2D1_BITMAP_PROPERTIES1 bitmapProperties =
				D2D1::BitmapProperties1(
					D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
					D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
				);

			D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(barWindow.w), static_cast<UINT32>(barWindow.h));

			barDeviceContext->CreateBitmap(
				size,
				nullptr,
				0,
				&bitmapProperties,
				&barBackgroundBitmap
			);

			barDeviceContext->QueryInterface(__uuidof(ID2D1GdiInteropRenderTarget), (void**)&barGdiInterop);

			barDeviceContext->SetTarget(barBackgroundBitmap);
			barDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		}

		chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();
		RECT original = RECT(0, 0, barWindow.w, barWindow.h), current = RECT(0, 0, 0, 0);

		wstring fps;
		for (int forNum = 1; !offSignal; forNum = 2)
		{
			// 计算 UI
			{
				// 主按钮
				{
					if (barState.fold)
					{
						superellipseMap[BarUISetSuperellipseEnum::MainButton]->n.value().tar = 3.0;
					}
					else
					{
						superellipseMap[BarUISetSuperellipseEnum::MainButton]->n.value().tar = 10.0;
					}
				}
				// 主栏
				{
					// 按钮位置计算（特别操作）
					double totalWidth = 5.0;
					{
						double xO = 5.0, yO = 5.0;
						// 控件计算的 xO 和 yO 包含自身和 右侧、下册 的空隙值 5px

						for (int id = 0; id < barButtomSet.tot; id++)
						{
							BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
							if (temp == nullptr) continue;

							if (temp->size == BarButtomSizeEnum::oneOne)
							{
								// 特殊设定：是否是颜色选择器
								bool isColorSelector = (temp->name.enable.tar && temp->name.content.GetTar().substr(0, 7) == L"__color");

								if (temp->buttom.enable.tar)
								{
									if (barState.fold)
									{
										temp->buttom.x.tar = 25.0;
										temp->buttom.y.tar = 25.0;

										temp->buttom.pct.tar = 0.0;
									}
									else
									{
										temp->buttom.x.tar = xO;
										if (yO <= 5.0) temp->buttom.y.tar = yO + 2.5; // 位于第一行
										else temp->buttom.y.tar = yO; // 位于第二行

										if (isColorSelector) temp->buttom.pct.tar = 1.0; // 只有颜色选择器使用
										else
										{
											if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.tar = 0.1;
											else if (temp->state->state == BarWidgetState::Selected) temp->buttom.pct.tar = 0.2;
											else temp->buttom.pct.tar = 0.0;
										}
									}
									temp->buttom.w.tar = 30.0;
									temp->buttom.h.tar = 30.0;

									if (!isColorSelector)
									{
										if (temp->state->emph == BarWidgetEmphasize::Pressed && temp->state->state != BarWidgetState::Selected)
											temp->buttom.fill.value().tar = RGB(127, 127, 127);
										else temp->buttom.fill.value().tar = RGB(88, 255, 236);
									}
								}
								if (temp->icon.enable.tar)
								{
									if (isColorSelector) temp->icon.SetWH(nullopt, 10.0); // 颜色选择器中的图标即为标识选中该颜色，所以需要较小尺寸
									else temp->icon.SetWH(nullopt, 20.0);

									temp->icon.x.tar = 0.0;
									temp->icon.y.tar = 0.0;
									if (barState.fold)
									{
										temp->icon.pct.tar = 0.0;
									}
									else
									{
										temp->icon.pct.tar = 1.0;

										if (barStyle.darkStyle)
										{
											if (temp->state->state == BarWidgetState::Selected) temp->icon.color1.value().tar = RGB(88, 255, 236);
											else temp->icon.color1.value().tar = RGB(255, 255, 255);
										}
										else
										{
											if (temp->state->state == BarWidgetState::Selected) temp->icon.color1.value().tar = RGB(88, 255, 236);
											else temp->icon.color1.value().tar = RGB(0, 0, 0);
										}
									}
								}
								if (temp->name.enable.tar)
								{
									// 无法容下文字的位置
									temp->name.pct.tar = 0.0;
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.tar = 0.0;
									temp->icon.pct.tar = 0.0;
									temp->name.pct.tar = 0.0;
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
									if (barState.fold)
									{
										temp->buttom.x.tar = 5.0;
										temp->buttom.y.tar = 25.0;

										temp->buttom.pct.tar = 0.0;
									}
									else
									{
										temp->buttom.x.tar = xO;
										if (yO <= 5.0) temp->buttom.y.tar = yO + 2.5; // 位于第一行
										else temp->buttom.y.tar = yO; // 位于第二行

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.tar = 0.1;
										else if (temp->state->state == BarWidgetState::Selected) temp->buttom.pct.tar = 0.2;
										else temp->buttom.pct.tar = 0.0;
									}
									temp->buttom.w.tar = 70.0;
									temp->buttom.h.tar = 30.0;

									if (temp->state->emph == BarWidgetEmphasize::Pressed && temp->state->state != BarWidgetState::Selected)
										temp->buttom.fill.value().tar = RGB(127, 127, 127);
									else temp->buttom.fill.value().tar = RGB(88, 255, 236);
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 18.0);

									temp->icon.x.tar = -21.0; // 靠左对齐（上下两侧均保持 6px 的空隙，而左侧是 5px）
									temp->icon.y.tar = 0.0;
									if (barState.fold) temp->icon.pct.tar = 0.0;
									else
									{
										temp->icon.pct.tar = 1.0;

										if (barStyle.darkStyle)
										{
											if (temp->state->state == BarWidgetState::Selected) temp->icon.color1.value().tar = RGB(88, 255, 236);
											else temp->icon.color1.value().tar = RGB(255, 255, 255);
										}
										else
										{
											if (temp->state->state == BarWidgetState::Selected) temp->icon.color1.value().tar = RGB(88, 255, 236);
											else temp->icon.color1.value().tar = RGB(27, 27, 27);
										}
									}
								}
								if (temp->name.enable.tar)
								{
									temp->name.x.tar = 11.5; // 右对齐
									temp->name.y.tar = 0.0;
									temp->name.w.tar = 37; // 70px 宽度中除去左侧 icon 占用的 18px + 5px * 2 的空隙,考虑自身右侧还有 5px 的间隙
									temp->name.h.tar = 30.0;
									if (barState.fold) temp->name.pct.tar = 0.0;
									else temp->name.pct.tar = 1.0;

									if (barStyle.darkStyle)
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.tar = RGB(88, 255, 236);
										else temp->name.color.tar = RGB(255, 255, 255);
									}
									else
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.tar = RGB(88, 255, 236);
										else temp->name.color.tar = RGB(27, 27, 27);
									}
									temp->name.size.tar = 12.0;
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.tar = 0.0;
									temp->icon.pct.tar = 0.0;
									temp->name.pct.tar = 0.0;
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
									if (barState.fold)
									{
										temp->buttom.x.tar = 5.0;
										temp->buttom.y.tar = 5.0;

										temp->buttom.pct.tar = 0.0;
									}
									else
									{
										temp->buttom.x.tar = xO;
										temp->buttom.y.tar = yO;

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.tar = 0.1;
										else if (temp->state->state == BarWidgetState::Selected) temp->buttom.pct.tar = 0.2;
										else temp->buttom.pct.tar = 0.0;
									}
									temp->buttom.w.tar = 70.0;
									temp->buttom.h.tar = 70.0;

									if (temp->state->emph == BarWidgetEmphasize::Pressed && temp->state->state != BarWidgetState::Selected)
										temp->buttom.fill.value().tar = RGB(127, 127, 127);
									else temp->buttom.fill.value().tar = RGB(88, 255, 236);
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 28.0);
									temp->icon.x.tar = 0.0;
									temp->icon.y.tar = -10.0;
									if (barState.fold)
									{
										temp->icon.pct.tar = 0.0;
									}
									else
									{
										temp->icon.pct.tar = 1.0;

										if (barStyle.darkStyle)
										{
											if (temp->state->state == BarWidgetState::Selected) temp->icon.color1.value().tar = RGB(88, 255, 236);
											else temp->icon.color1.value().tar = RGB(255, 255, 255);
										}
										else
										{
											if (temp->state->state == BarWidgetState::Selected) temp->icon.color1.value().tar = RGB(88, 255, 236);
											else temp->icon.color1.value().tar = RGB(27, 27, 27);
										}
									}
								}
								if (temp->name.enable.tar)
								{
									temp->name.x.tar = 0.0;
									temp->name.y.tar = 20.0;
									temp->name.w.tar = 70.0;
									temp->name.h.tar = 25.0;
									if (barState.fold) temp->name.pct.tar = 0.0;
									else temp->name.pct.tar = 1.0;

									if (barStyle.darkStyle)
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.tar = RGB(88, 255, 236);
										else temp->name.color.tar = RGB(255, 255, 255);
									}

									else
									{
										if (temp->state->state == BarWidgetState::Selected) temp->name.color.tar = RGB(88, 255, 236);
										else temp->name.color.tar = RGB(27, 27, 27);
									}

									temp->name.size.tar = 13.0;
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.tar = 0.0;
									temp->icon.pct.tar = 0.0;
									temp->name.pct.tar = 0.0;
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
									if (barState.fold)
									{
										temp->buttom.x.tar = 35.0;
										temp->buttom.y.tar = 5.0;

										temp->buttom.pct.tar = 0.0;
									}
									else
									{
										temp->buttom.x.tar = xO;
										temp->buttom.y.tar = yO;

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.pct.tar = 0.2;
										else temp->buttom.pct.tar = 0.0;
									}
									temp->buttom.w.tar = 10.0;
									temp->buttom.h.tar = 70.0;

									if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->buttom.fill.value().tar = RGB(127, 127, 127);
									else temp->buttom.fill.value().tar = RGB(88, 255, 236);
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 60.0);
									if (barState.fold)
									{
										temp->icon.pct.tar = 0.0;
									}
									else
									{
										temp->icon.pct.tar = 0.18;
										if (barStyle.darkStyle) temp->icon.color1.value().tar = RGB(255, 255, 255);
										else temp->icon.color1.value().tar = RGB(0, 0, 0);
									}
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (temp->hide)
								{
									temp->buttom.pct.tar = 0.0;
									temp->icon.pct.tar = 0.0;
									temp->name.pct.tar = 0.0;
								}
								else
								{
									xO += 15, yO = 5.0;
									totalWidth += 15;
								}
							}
						}
					}

					// 主栏
					{
						if (barState.fold)
						{
							shapeMap[BarUISetShapeEnum::MainBar]->x.tar = 0.0;
							shapeMap[BarUISetShapeEnum::MainBar]->w.tar = 80.0;

							shapeMap[BarUISetShapeEnum::MainBar]->pct.tar = 0.0;
							shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().tar = 0.0;
						}
						else
						{
							shapeMap[BarUISetShapeEnum::MainBar]->w.tar = totalWidth;
							if (barState.widgetPosition.mainBar)
								shapeMap[BarUISetShapeEnum::MainBar]->x.tar = superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + shapeMap[BarUISetShapeEnum::MainBar]->w.tar / 2.0 + 10.0;
							else
								shapeMap[BarUISetShapeEnum::MainBar]->x.tar = -(superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + shapeMap[BarUISetShapeEnum::MainBar]->w.tar / 2.0 + 10.0);

							shapeMap[BarUISetShapeEnum::MainBar]->pct.tar = 0.8;
							shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().tar = 0.18;
						}
						if (barStyle.darkStyle)
						{
							shapeMap[BarUISetShapeEnum::MainBar]->fill.value().tar = RGB(24, 24, 24);
							shapeMap[BarUISetShapeEnum::MainBar]->frame.value().tar = RGB(255, 255, 255);
						}
						else
						{
							shapeMap[BarUISetShapeEnum::MainBar]->fill.value().tar = RGB(243, 243, 243);
							shapeMap[BarUISetShapeEnum::MainBar]->frame.value().tar = RGB(0, 0, 0);
						}

						// 绘制属性
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->x.tar = 5.0;
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->y.tar = 0.0;
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->w.tar = 60.0;
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->h.tar = 60.0;

								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->pct.tar = 0.0;
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->framePct.value().tar = 0.0;
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->w.tar = 335.0;
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->h.tar = 120.0;

								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->x.tar = -(barButtomSet.preset[(int)BarButtomPresetEnum::Draw]->lastDrawX);
								if (barState.widgetPosition.primaryBar)
									shapeMap[BarUISetShapeEnum::DrawAttributeBar]->y.tar = (shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + shapeMap[BarUISetShapeEnum::DrawAttributeBar]->GetH() / 2.0 + 10.0);
								else
									shapeMap[BarUISetShapeEnum::DrawAttributeBar]->y.tar = -(shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + shapeMap[BarUISetShapeEnum::DrawAttributeBar]->GetH() / 2.0 + 10.0);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->pct.tar = 0.9;
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->framePct.value().tar = 0.18;
							}
							if (barStyle.darkStyle)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->fill.value().tar = RGB(24, 24, 24);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->frame.value().tar = RGB(255, 255, 255);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->fill.value().tar = RGB(243, 243, 243);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar]->frame.value().tar = RGB(0, 0, 0);
							}

							// Color 区域
							{
								// Color 1
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.tar = 5.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.tar = 50.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->fill.value().tar))
									{
										// 说明当前选中的是当前的颜色
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().tar = 1.0;
									}
								}
								// Color 2
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.tar = 5.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.tar = 40.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.tar = 85.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().tar = 1.0;
									}
								}
								// Color 3
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.tar = 40.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.tar = 50.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().tar = 1.0;
									}
								}
								// Color 4
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.tar = 40.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.tar = 40.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.tar = 85.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().tar = 1.0;
									}
								}
								// Color 5
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.tar = 75.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.tar = 50.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().tar = 1.0;
									}
								}
								// Color 6
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.tar = 75.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.tar = 40.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.tar = 85.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().tar = 1.0;
									}
								}
								// Color 7
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.tar = 110.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.tar = 50.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().tar = 1.0;
									}
								}
								// Color 8
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.tar = 110.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.tar = 40.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.tar = 85.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().tar = 1.0;
									}
								}
								// Color 9
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.tar = 145.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.tar = 50.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().tar = 1.0;
									}
								}
								// Color 10
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.tar = 145.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.tar = 40.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.tar = 85.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().tar = 1.0;
									}
								}
								// Color 11
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.tar = 15.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.tar = 15.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.tar = 180.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.tar = 50.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.tar = 1.0;
									}

									if (barState.drawAttribute && IdtColor::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->fill.value().tar))
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.tar = 1.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().tar = 2.0;
									}
									else
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().tar = 1.0;
									}
								}
							}
							{ /**/ }
							// 画笔样式区域
							{
								// 画笔
								{
									if (!barState.drawAttribute)
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->x.tar = 5.0;
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->y.tar = 0.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct.tar = 0.0;

										svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->pct.tar = 0.0;
										wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->x.tar = 5.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->y.tar = 0.0;
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->x.tar = 0.0;
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->y.tar = 5.0;

										if (barState.drawAttributeBar.brush1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1) shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct.tar = 0.1;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct.tar = 0.0;

										svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->pct.tar = 1.0;
										wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->pct.tar = 1.0;
									}
									if (barStyle.darkStyle)
									{
										if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.tar = RGB(88, 255, 236);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->color1.value().tar = RGB(88, 255, 236);
										}
										else
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.tar = RGB(255, 255, 255);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->color1.value().tar = RGB(255, 255, 255);
										}
									}
									else
									{
										if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.tar = RGB(88, 255, 236);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->color1.value().tar = RGB(88, 255, 236);
										}
										else
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Brush1]->color.tar = RGB(24, 24, 24);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->color1.value().tar = RGB(24, 24, 24);
										}
									}

									if (barState.drawAttributeBar.brush1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1)
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->fill.value().tar = RGB(127, 127, 127);
									else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->fill.value().tar = RGB(88, 255, 236);
								}
								// 荧光笔
								{
									if (!barState.drawAttribute)
									{
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->x.tar = 5.0;
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->y.tar = 0.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct.tar = 0.0;

										svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->pct.tar = 0.0;
										wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->x.tar = 60.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->y.tar = 0.0;
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->x.tar = 0.0;
										svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->y.tar = 5.0;

										if (barState.drawAttributeBar.highlight1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1) shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct.tar = 0.1;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct.tar = 0.0;

										svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->pct.tar = 1.0;
										wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->pct.tar = 1.0;
									}
									if (barStyle.darkStyle)
									{
										if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.tar = RGB(88, 255, 236);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->color1.value().tar = RGB(88, 255, 236);
										}
										else
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.tar = RGB(255, 255, 255);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->color1.value().tar = RGB(255, 255, 255);
										}
									}
									else
									{
										if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.tar = RGB(88, 255, 236);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->color1.value().tar = RGB(88, 255, 236);
										}
										else
										{
											wordMap[BarUISetWordEnum::DrawAttributeBar_Highlight1]->color.tar = RGB(24, 24, 24);
											svgMap[BarUISetSvgEnum::DrawAttributeBar_Highlight1]->color1.value().tar = RGB(24, 24, 24);
										}
									}

									if (barState.drawAttributeBar.highlight1Press && stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1)
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->fill.value().tar = RGB(127, 127, 127);
									else shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->fill.value().tar = RGB(88, 255, 236);
								}

								// 选中
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.tar = 5.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->pct.tar = 0.0;
									}
									else
									{
										if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
											shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.tar = 60.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->x.tar = 5.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->pct.tar = 0.2;
									}
									if (barStyle.darkStyle)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->fill.value().tar = RGB(88, 255, 236);
									}
									else
									{
										// TODO
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelect]->fill.value().tar = RGB(88, 255, 236);
									}
								}
								// 选中滑动槽
								{
									if (!barState.drawAttribute)
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->x.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.tar = 0.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->w.tar = 60.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->h.tar = 60.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->pct.tar = 0.0;
									}
									else
									{
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->x.tar = 215.0;
										if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.tar = 5.0;
										else shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->y.tar = 50.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->w.tar = 115.0;
										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->h.tar = 65.0;

										shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]->pct.tar = 0.15;
									}
								}
							}
							// 粗细调节区域
							{
								if (!barState.drawAttribute)
								{
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->x.tar = 0.0;
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.tar = 0.0;
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->w.tar = 60.0;
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->h.tar = 60.0;

									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->pct.tar = 0.0;
								}
								else
								{
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->x.tar = 5.0;
									if (barState.widgetPosition.primaryBar) shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.tar = 75.0;
									else shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->y.tar = 5.0;
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->w.tar = 205.0;
									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->h.tar = 40.0;

									shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect]->pct.tar = 0.15;
								}

								if (!barState.drawAttribute)
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->w.tar = 30.0;
									wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->pct.tar = 0.0;
								}
								else
								{
									wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->w.tar = 80.0;
									wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->pct.tar = 1.0;
								}
							}
						}
					}
				}
			}

			// 动效 UI
			bool needRendering = false;
			{
				auto ChangeState = [&](BarUiStateClass& state, bool forceReplace) -> void
					{
						needRendering = true;
						state.val = state.tar;
					};
				auto ChangeValue = [&](BarUiValueClass& value, bool forceReplace) -> void
					{
						needRendering = true;
						BarUiValueModeEnum mod = value.mod;
						/*else*/ value.val = value.tar;
					};
				auto ChangeColor = [&](BarUiColorClass& color, bool forceReplace) -> void
					{
						needRendering = true;
						color.val = color.tar;
					};
				auto ChangePct = [&](BarUiPctClass& pct, bool forceReplace) -> void
					{
						needRendering = true;
						pct.val = pct.tar;
					};
				auto ChangeString = [&](BarUiStringClass& stringO, bool forceReplace) -> void
					{
						needRendering = true;
						stringO.ApplyTar();
					};

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
					if (val->svg.IsSame()) ChangeString(val->svg, forceReplace), change = true;
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
			}
			{ /**/ }

			// 渲染 UI
			{
				current = RECT(0, 0, 0, 0);
				barDeviceContext->BeginDraw();

				// 清除背景
				{
					D2D1_COLOR_F clearColor = IdtColor::ConvertToD2dColor(RGBA(0, 0, 0, 0));
					barDeviceContext->Clear(&clearColor);

					// TODO 绘制纯白全透明警告用户开启 aero
					auto obj = BarUISetWordEnum::BackgroundWarning;
					spec.Word(barDeviceContext, *wordMap[obj], wordMap[obj]->Inherit(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
				}

				using enum BarUiInheritEnum;
				{
					// 主栏
					{
						// 提前计算依赖
						{
							superellipseMap[BarUISetSuperellipseEnum::MainButton]->Inherit();
							shapeMap[BarUISetShapeEnum::MainBar]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]);
							barButtomSet.preset[(int)BarButtomPresetEnum::Draw]->buttom.Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::MainBar]);
						}

						// 绘制属性
						{
							auto obj = BarUISetShapeEnum::DrawAttributeBar;
							spec.Shape(barDeviceContext, *shapeMap[obj], shapeMap[obj]->Inherit(Left, barButtomSet.preset[(int)BarButtomPresetEnum::Draw]->buttom), &current, true);

							// Color 区域
							{
								// Color 1
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect1;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect1;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 2
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect2;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect2;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 3
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect3;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect3;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 4
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect4;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect4;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 5
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect5;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect5;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 6
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect6;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect6;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 7
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect7;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect7;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 8
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect8;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect8;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 9
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect9;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect9;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 10
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect10;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect10;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
								// Color 11
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect11;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect11;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Center, *shapeMap[obj1]));
								}
							}
							// 画笔样式区域
							{
								// 选中滑动槽
								{
									auto obj = BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove;
									spec.Shape(barDeviceContext, *shapeMap[obj], shapeMap[obj]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));
								}
								// 选中
								{
									auto obj = BarUISetShapeEnum::DrawAttributeBar_DrawSelect;
									spec.Shape(barDeviceContext, *shapeMap[obj], shapeMap[obj]->Inherit(Left, *shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]));
								}

								// 画笔
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_Brush1;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(Left, *shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_Brush1;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Top, *shapeMap[obj1]));

									auto obj3 = BarUISetWordEnum::DrawAttributeBar_Brush1;
									spec.Word(barDeviceContext, *wordMap[obj3], wordMap[obj3]->Inherit(ToBottom, *svgMap[obj2]));
								}
								// 荧光笔
								{
									auto obj1 = BarUISetShapeEnum::DrawAttributeBar_Highlight1;
									spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(Left, *shapeMap[BarUISetShapeEnum::DrawAttributeBar_DrawSelectGroove]));

									auto obj2 = BarUISetSvgEnum::DrawAttributeBar_Highlight1;
									spec.Svg(barDeviceContext, *svgMap[obj2], svgMap[obj2]->Inherit(Top, *shapeMap[obj1]));

									auto obj3 = BarUISetWordEnum::DrawAttributeBar_Highlight1;
									spec.Word(barDeviceContext, *wordMap[obj3], wordMap[obj3]->Inherit(ToBottom, *svgMap[obj2]));
								}
							}
							// 粗细调节区域
							{
								auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect;
								spec.Shape(barDeviceContext, *shapeMap[obj1], shapeMap[obj1]->Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

								auto obj2 = BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay;
								wordMap[obj2]->Inherit(Right, *shapeMap[obj1]); // 提前计算依赖

								// 自定义绘制：粗细预览
								if (wordMap[obj2]->pct.val > 0.0)
								{
									FLOAT penThickness = static_cast<FLOAT>(GetPenWidth());

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

									auto tarRect = D2D1::RectF(
										static_cast<FLOAT>(tarX) * tarZoom,
										static_cast<FLOAT>(tarY) * tarZoom,
										static_cast<FLOAT>(tarEndX) * tarZoom,
										static_cast<FLOAT>(tarEndY) * tarZoom);

									// ==== 创建圆角矩形几何 ====
									CComPtr<ID2D1Factory> factory;
									barDeviceContext->GetFactory(&factory);

									CComPtr<ID2D1RoundedRectangleGeometry> roundedRectGeo;
									D2D1_ROUNDED_RECT roundedRect = {
										tarRect,
										static_cast<FLOAT>(tarRw) * tarZoom,
										static_cast<FLOAT>(tarRh) * tarZoom
									};
									factory->CreateRoundedRectangleGeometry(roundedRect, &roundedRectGeo);

									// ==== 创建 Layer ====
									CComPtr<ID2D1Layer> layer;
									barDeviceContext->CreateLayer(&layer);

									// ==== 启用裁切层 ====
									D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters();
									layerParams.geometricMask = roundedRectGeo;
									layerParams.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;

									barDeviceContext->PushLayer(&layerParams, layer);

									// ====== 四个经过点（百分比） ======
									auto w = tarRect.right - tarRect.left;
									auto h = tarRect.bottom - tarRect.top;

									D2D1_POINT_2F p1 = { clamp(tarRect.left + penThickness / 2.0f,
														clamp(tarRect.left + 5.0f * tarZoom, 0.0f, tarRect.left + w * 0.5f), tarRect.left + w * 0.5f),
														tarRect.top + h * 0.50f };
									D2D1_POINT_2F p4 = { clamp(tarRect.left + w - penThickness / 2.0f,
															tarRect.left + w * 0.5f, clamp(tarRect.left + w - 5.0f * tarZoom, tarRect.left + w * 0.5f, tarRect.left + w)),
														tarRect.top + h * 0.50f };

									D2D1_POINT_2F p2 = { tarRect.left + (p4.x - p1.x) / 3.0f,
														clamp(tarRect.top + penThickness / 2.0f,
															clamp(0.0f, tarRect.top + 5.0f * tarZoom, tarRect.top + h * 0.5f), tarRect.top + h * 0.5f) };
									D2D1_POINT_2F p3 = { tarRect.left + (p4.x - p1.x) * 2.0f / 3.0f,
														clamp(tarRect.top + h - penThickness / 2.0f,
															tarRect.top + h * 0.5f, clamp(tarRect.top + h - 5.0f * tarZoom, tarRect.top + h * 0.5f, tarRect.top + h)) };

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
									CComPtr<ID2D1PathGeometry> pathGeometry;
									factory->CreatePathGeometry(&pathGeometry);

									CComPtr<ID2D1GeometrySink> sink;
									pathGeometry->Open(&sink);

									sink->BeginFigure(pts.front(), D2D1_FIGURE_BEGIN_HOLLOW);
									for (auto& bz : beziers) sink->AddBezier(bz);
									sink->EndFigure(D2D1_FIGURE_END_OPEN);

									sink->Close();

									// ==== 画刷 ====
									CComPtr<ID2D1SolidColorBrush> brush;
									barDeviceContext->CreateSolidColorBrush(
										D2D1::ColorF(IdtColor::ConvertToD2dColor(color, tarPct)),
										&brush
									);

									// ==== Stroke Style（圆头、圆角）====
									CComPtr<ID2D1StrokeStyle> strokeStyle;
									D2D1_STROKE_STYLE_PROPERTIES props{};
									props.startCap = D2D1_CAP_STYLE_ROUND;
									props.endCap = D2D1_CAP_STYLE_ROUND;
									props.lineJoin = D2D1_LINE_JOIN_ROUND;
									factory->CreateStrokeStyle(&props, nullptr, 0, &strokeStyle);

									// ==== 绘制贝塞尔曲线（裁切生效）====
									barDeviceContext->DrawGeometry(pathGeometry, brush, penThickness, strokeStyle);

									// ==== 结束裁切 ====
									barDeviceContext->PopLayer();
								}

								// obj2
								spec.Word(barDeviceContext, *wordMap[obj2], wordMap[obj2]->Inherit(Right, *shapeMap[obj1]), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
							}
						}

						// 主栏
						auto obj = BarUISetShapeEnum::MainBar;
						spec.Shape(barDeviceContext, *shapeMap[obj], shapeMap[obj]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]), &current, true);

						// 主栏按钮
						for (int id = 0; id < barButtomSet.tot; id++)
						{
							BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
							if (temp == nullptr) continue;

							spec.Shape(barDeviceContext, temp->buttom, temp->buttom.Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::MainBar]));
							spec.Svg(barDeviceContext, temp->icon, temp->icon.Inherit(Center, temp->buttom));
							spec.Word(barDeviceContext, temp->name, temp->name.Inherit(Center, temp->buttom));
						}
					}
					{ /**/ }

					// 主按钮
					{
						auto obj = BarUISetSuperellipseEnum::MainButton;
						spec.Superellipse(barDeviceContext, *superellipseMap[obj], superellipseMap[obj]->Inherit(), &current, true);

						{
							auto obj = BarUISetSvgEnum::logo1;
							spec.Svg(barDeviceContext, *svgMap[obj], svgMap[obj]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]));
						}
					}
				}
				{ /**/ }

				// 调试 + FPS
				{
					double tarZoom = barStyle.zoom;
					wstring content = L"开发版本 " + editionDate + L" | 不代表最终品质 | " + fps;

					CComPtr<IDWriteTextFormat> pTextFormat;
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
					CComPtr<ID2D1SolidColorBrush> pBrush;
					barDeviceContext->CreateSolidColorBrush(
						D2D1::ColorF(D2D1::ColorF(255, 255, 255, 0.5)),
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

				// TODO 脏区更新
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

					CComPtr<ID2D1SolidColorBrush> spBorderBrush;
					barDeviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(frame, 1.0), &spBorderBrush);

					// barDeviceContext->DrawRoundedRectangle(&roundedRect, spBorderBrush, 1.0f);
				}

				barDeviceContext->Flush();

				{
					// TODO 脏区更新
					/*RECT target = original;
					original = current;
					BarRenderingAttribute::UnionRectInPlace(target, current);*/

					// 上方已经给出了计算脏区 target 的代码，这里直接使用 RECT target 即可

					// psize 指定窗口本次更新“新内容”宽高
					// pptDst 指定新内容贴到屏幕上的位置（左上角）
					// pptSrc 从源内存 DC 的哪个位置起贴内容

					// 设置窗口位置
					POINT ptDst = { 0, 0 };
					// 获取 DC
					HDC hdc = nullptr;
					barGdiInterop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &hdc);

					ulwi.pptDst = &ptDst;
					ulwi.hdcSrc = hdc;
					ulwi.prcDirty = &target;
					UpdateLayeredWindowIndirect(floating_window, &ulwi);

					barGdiInterop->ReleaseDC(nullptr);
				}

				barDeviceContext->EndDraw();
				barMedia.formatCache->Clean();
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
	void Interact()
	{
		ExMessage msg;
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

						if (temp->buttom.IsClick(msg.x, msg.y, barStyle.zoom))
						{
							continueFlag = false;
							if (msg.message == WM_LBUTTONDOWN /*msg.lbutton*/)
							{
								temp->state->emph = BarWidgetEmphasize::Pressed; UpdateRendering(false);
								while (true)
								{
									hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);
									if (temp->buttom.IsClick(msg.x, msg.y, barStyle.zoom))
									{
										if (!msg.lbutton)
										{
											if (temp->clickFunc) temp->clickFunc();
											UpdateRendering();

											break;
										}
									}
									else break;
								}
								temp->state->emph = BarWidgetEmphasize::None; UpdateRendering(false);

								hiex::flushmessage_win32(EM_MOUSE, floating_window);
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
									SetPenColor(IdtColor::SetAlphaR(obj->fill.value().tar, 255));
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

public:
	BarWindowPosClass barWindow;
	BarMediaClass barMedia;
	BarButtomSetClass barButtomSet;
	BarUIRendering spec;

	BarStateClass barState;
	BarStyleClass barStyle;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<BarUiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum, shared_ptr<BarUiSuperellipseClass>> superellipseMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<BarUiSVGClass>> svgMap;
	ankerl::unordered_dense::map<BarUISetWordEnum, shared_ptr<BarUiWordClass>> wordMap;

public:
	// 渲染更新：状态更新 + 通知计算并渲染
	void UpdateRendering(bool updateState = true)
	{
		// 状态更新
		if (updateState)
		{
			barButtomSet.StateUpdate();
			barState.ThicknessDisplayUpdate();
		}
		// TODO 通知计算并渲染
	}
protected:
	// 拖动交互
	double Seek(const ExMessage& msg)
	{
		if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) return 0;

		POINT p;
		GetCursorPos(&p);

		double firX = static_cast<double>(p.x);
		double firY = static_cast<double>(p.y);

		double ret = 0.0;

		while (1)
		{
			if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;
			GetCursorPos(&p);

			if (firX == p.x && firY == p.y)
			{
				this_thread::sleep_for(chrono::milliseconds(15));
				continue;
			}

			double tarZoom = barStyle.zoom;
			superellipseMap[BarUISetSuperellipseEnum::MainButton]->x.tar += static_cast<double>(p.x - firX) / tarZoom;
			superellipseMap[BarUISetSuperellipseEnum::MainButton]->y.tar += static_cast<double>(p.y - firY) / tarZoom;

			// TODO 没办法跑到窗口外面

			ret += sqrt((p.x - firX) * (p.x - firX) + (p.y - firY) * (p.y - firY));
			firX = static_cast<double>(p.x), firY = static_cast<double>(p.y);

			// 更新位置状态
			barState.PositionUpdate(tarZoom);
			// 拖动时收起主栏
			if (setlist.regularSetting.moveRecover)
			{
				if (ret > 20 && barState.fold == false)
				{
					barState.fold = true;
				}
			}

			UpdateRendering();
		}

		return ret;
	}
} barUISet; // 全局 Bar UI 集合

// ====================
// 环境

// 弃用
/*
// LOGO配色方案
enum class BarLogoColorSchemeEnum : int
{
	Default = 0, // 深色
	Slate = 1, // 浅色
};*/

// 初始化

export class BarInitializationClass
{
private:
	BarInitializationClass() = delete;
public:
	static void Initialization()
	{
		threadStatus[L"BarInitialization"] = true;

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
			if (!threadStatus[L"BarUI"] &&
				!threadStatus[L"BarDraw"]) break;
			this_thread::sleep_for(chrono::milliseconds(500));
		}

		threadStatus[L"BarInitialization"] = false;
		return;
	}

protected:
	static void InitializeWindow(BarUISetClass& barUISet)
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
	static void InitializeMedia(BarUISetClass& barUISet)
	{
		barUISet.barMedia.LoadExImage();
	}
	static void InitializeUI(BarUISetClass& barUISet)
	{
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
				auto superellipse = make_shared<BarUiSuperellipseClass>(100.0, 100.0, 80.0, 80.0, 3.0, 1.0, RGB(24, 24, 24), RGB(255, 255, 255));
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
};