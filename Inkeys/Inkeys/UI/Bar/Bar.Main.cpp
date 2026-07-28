module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtD2DPreparation.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtState.h"
#include "../../../IdtWindow.h"
#include <d2d1effects.h>

#pragma comment(lib, "dxguid.lib")

// ====================
// 临时
extern IdtAtomic<bool> ConfirmaNoMouMsgSignal, ConfirmaNoMouFunSignal;
void FloatingInstallHook();

module Inkeys.UI.Bar;
import :Main;
import :Atomic;
import :Zoom;
import :Theme;

import <ranges>;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;

constexpr double BarButtonHoverOpacity = 0.18;
constexpr double BarButtonPressScale = 0.95;
constexpr double BarButtonHoverShowDur = 0.24;
constexpr double BarButtonHoverExitDur = 0.24;
constexpr double BarButtonHoverFadeDur = 5.0;
constexpr double BarBorderLightRadius = 480.0;
constexpr double BarBorderCursorFadeInDur = 0.30;
constexpr double BarBorderCursorLightRadius = 240.0;
constexpr ULONGLONG BarBorderCursorGraceDurationMs = 5000;
constexpr UINT_PTR BarBorderCursorGraceTimerId = 0x494B4301;
constexpr UINT BarBorderCursorSuspendMessage = WM_APP + 0x31;
constexpr UINT BarCanvasDrawingActivityMessage = WM_APP + 0x32;
constexpr double BarBorderLightIntensity = 1.0;
constexpr double BarColorSwatchCursorLightIntensity = 0.50;
constexpr double BarButtonCursorLightIntensity = 0.30;
constexpr double BarButtonPressedLightOpacity = 0.5;
constexpr double BarBrushThicknessPresetDip[] = { 1.0, 3.0, 6.0 };
constexpr double BarDrawAttributeExpandedWidth = 370.0;
constexpr double BarDrawAttributeExpandedHeight = 185.0;
constexpr double BarDrawAttributeCompactWidth = 60.0;
constexpr double BarDrawAttributeCompactScale =
	BarDrawAttributeCompactWidth / BarDrawAttributeExpandedWidth;
constexpr double BarDrawAttributeCompactHeight =
	BarDrawAttributeExpandedHeight * BarDrawAttributeCompactScale;
constexpr double BarDrawAttributeGap = 5.0;
constexpr double BarDrawAttributeThicknessHeight = 105.0;
constexpr double BarDrawAttributeThicknessControlHeight = 30.0;
constexpr double BarDrawAttributeSurfaceOpacity = 0.95;
constexpr double BarDrawAttributeThicknessContentInset =
	BarDrawAttributeGap * 2.0;
constexpr double BarThicknessTooltipBadgeHeight = 24.0;
constexpr double BarThicknessTooltipIconSize = 14.0;
constexpr double BarThicknessTooltipCloseButtonSize = 20.0;
constexpr double BarThicknessTooltipHitPadding = 2.0;
constexpr bool BarThicknessOverflowPreviewAlwaysVisible = true;
constexpr double BarThicknessTooltipPadding = 8.0;
constexpr double BarThicknessTooltipCloseReserve = 25.0;
constexpr double BarThicknessTooltipTitleFontSize = 12.0;
constexpr double BarThicknessTooltipBodyFontSize = 10.0;
constexpr double BarThicknessTooltipLineGap = 3.0;
constexpr double BarThicknessTooltipPopupGap =
	BarDrawAttributeThicknessContentInset;
constexpr double BarThicknessTooltipFillOpacity =
	BarDrawAttributeSurfaceOpacity;
constexpr double BarThicknessTooltipFrameOpacity = 0.18;
constexpr double BarBorderFrameDiffuseOpacity = 0.30;
constexpr double BarBorderPenDiffuseOpacity = 0.20;
constexpr double BarColorSwatchFrameOpacity = 0.18;
constexpr int BarBorderDiffuseCompositePasses = 2;
// 标准差等于线宽时，1px 线源经过一维 Gaussian 后中心约保留 38.3%。
constexpr double BarBorderGaussianCenterCoverage = 0.382924922548;
std::atomic_uint BarCanvasDrawingActivityCount = 0;

bool PenModeUsesCurvedThicknessPreview(PenModeSelectEnum mode)
{
	// 未来软笔、激光笔接入实际模式枚举后，只需在这里扩展。
	return mode == PenModeSelectEnum::IdtPenBrush1;
}

bool PenModeSupportsAnnotationLine(PenModeSelectEnum mode)
{
	// 激光笔不显示标注线入口；未来软笔模式在这里加入。
	return mode == PenModeSelectEnum::IdtPenBrush1
		|| mode == PenModeSelectEnum::IdtPenHighlighter1;
}

int GetBarBrushThicknessPresetPx(size_t index, double dpiZoom)
{
	if (index >= 3 || !isfinite(dpiZoom) || dpiZoom <= 0.0) return 1;
	// 预设只跟随系统 DPI，不能再叠加 UI 配置缩放。
	return max(1, static_cast<int>(lround(BarBrushThicknessPresetDip[index] * dpiZoom)));
}

COLORREF GetBarReadableTextColor(COLORREF background)
{
	auto LinearChannel = [](BYTE channel)
		{
			double value = static_cast<double>(channel) / 255.0;
			return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
		};
	double luminance = 0.2126 * LinearChannel(GetRValue(background))
		+ 0.7152 * LinearChannel(GetGValue(background))
		+ 0.0722 * LinearChannel(GetBValue(background));
	return luminance > 0.179 ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

double ApplyBorderLightSmoothstep(double progress)
{
	progress = clamp(progress, 0.0, 1.0);
	return progress * progress * (3.0 - 2.0 * progress);
}

COLORREF MixBarUiColor(COLORREF startColor, COLORREF targetColor, double progress)
{
	// UI 颜色动画统一按 RGB 三通道共享同一条曲线进度。
	progress = clamp(progress, 0.0, 1.0);
	auto MixChannel = [progress](BYTE start, BYTE target)
		{
			double value = static_cast<double>(start)
				+ static_cast<double>(static_cast<int>(target) - static_cast<int>(start)) * progress;
			return static_cast<BYTE>(clamp(value, 0.0, 255.0) + 0.5);
		};
	return RGB(
		MixChannel(GetRValue(startColor), GetRValue(targetColor)),
		MixChannel(GetGValue(startColor), GetGValue(targetColor)),
		MixChannel(GetBValue(startColor), GetBValue(targetColor)));
}

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
	case WM_INPUT:
	{
		// Raw Input 只负责唤醒并读取系统光标，WM_INPUT 仍交给默认过程完成清理。
		barUISet.RegisterBorderCursorLight(hWnd);
		return HIWINDOW_DEFAULT_PROC;
	}

	case WM_TIMER:
	{
		if (wParam == BarBorderCursorGraceTimerId)
		{
			barUISet.HandleBorderCursorGraceTimeout(hWnd);
			return 0;
		}
		return HIWINDOW_DEFAULT_PROC;
	}

	case BarBorderCursorSuspendMessage:
	{
		barUISet.SuspendBorderCursorTracking(hWnd, wParam != 0);
		return 0;
	}

	case BarCanvasDrawingActivityMessage:
	{
		barUISet.HandleCanvasDrawingActivity(hWnd, wParam != 0);
		return 0;
	}

	case WM_MOUSELEAVE:
	{
		// 需要等待离开的休眠路径在真实移出后解除重新激活限制。
		lock_guard lock(barUISet.borderCursorLightMutex);
		barUISet.borderCursorActivationBlockedUntilLeave = false;
		return 0;
	}

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
		if (msg == WM_MOUSEMOVE) barUISet.ActivateBorderCursorTracking(hWnd);
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

void BarUIRendering::DiscardDeviceResources()
{
	frameGradientBrushCache.clear();
	frameDiffuseMaskCache.clear();
	frameGeometryDiffuseMaskCache.clear();
	frameSolidColorBrush.Reset();
	thicknessPreviewGradientBrush.Reset();
	thicknessPreviewPath.Reset();
	thicknessPreviewStrokeStyle.Reset();
	frameGaussianBlurEffect.Reset();
	frameMaskDeviceContext.Reset();
	frameGradientFailureLogged = false;
	thicknessPreviewGradientFailureLogged = false;
	thicknessPreviewGradientUnavailable = false;
	thicknessPreviewGradientColorInitialized = false;
	thicknessPreviewGradientLeftOpacity = -1.0F;
	thicknessPreviewPathFailureLogged = false;
	thicknessPreviewPathUnavailable = false;
	thicknessPreviewPathInitialized = false;
	frameDiffuseEffectFailureLogged = false;
	frameDiffuseMaskFailureLogged = false;
	frameDiffuseMaskUnavailable = false;
	frameDiffuseMaskCreatedThisFrame = false;
	frameDiffuseMaskGeometryScale = 1.0;
	frameDirtyClipActive = false;
	frameDirtyClipRect = {};
}

void BarUIRendering::PushFrameDirtyClip(
	ID2D1DeviceContext* deviceContext, const D2D1_RECT_F& dirtyRect)
{
	if (!deviceContext || frameDirtyClipActive) return;
	frameDirtyClipRect = dirtyRect;
	deviceContext->PushAxisAlignedClip(
		dirtyRect, D2D1_ANTIALIAS_MODE_ALIASED);
	frameDirtyClipActive = true;
}

void BarUIRendering::PopFrameDirtyClip(ID2D1DeviceContext* deviceContext)
{
	if (!deviceContext || !frameDirtyClipActive) return;
	deviceContext->PopAxisAlignedClip();
	frameDirtyClipActive = false;
}

void BarUIRendering::HandleFrameEndDrawResult(HRESULT endDrawResult)
{
	if (SUCCEEDED(endDrawResult))
	{
		frameDiffuseMaskCreatedThisFrame = false;
		return;
	}
	if (!frameDiffuseMaskCreatedThisFrame) return;

	// A8/Effect 错误可能只在 EndDraw 暴露；本会话立即降级，不能进入逐帧重试。
	frameDiffuseMaskCache.clear();
	frameGeometryDiffuseMaskCache.clear();
	frameGaussianBlurEffect.Reset();
	frameDiffuseMaskUnavailable = true;
	frameDiffuseMaskCreatedThisFrame = false;
	if (!frameDiffuseMaskFailureLogged)
	{
		frameDiffuseMaskFailureLogged = true;
		if (IDTLogger) IDTLogger->error(
			"[BarUIRendering::HandleFrameEndDrawResult] A8 预模糊遮罩提交失败，本设备停用柔光遮罩, hr=0x{:08X}",
			static_cast<unsigned int>(endDrawResult));
	}
}

bool BarUIRendering::PrepareFrameLighting(double animationDtSeconds)
{
	frameCursorLightVisible = false;
	frameDrawingUsesPenColor = false;
	COLORREF desiredDrawingPenColor = frameDrawingPenColorInitialized
		? frameDrawingPenColorTarget : (GetPenColor() & 0x00FFFFFF);

	double zoom = barUISetClass ? static_cast<double>(barUISetClass->barStyle.zoom) : 0.0;
	if (!isfinite(zoom) || zoom <= 0.0) zoom = 0.0;
	frameLightRadius = static_cast<FLOAT>(BarBorderLightRadius * zoom);
	frameCursorLightRadius = static_cast<FLOAT>(BarBorderCursorLightRadius * zoom);

	bool edgeLightingEnabled = BarUiEdgeLightingEnabled;
	bool dynamicEdgeLightingEnabled = edgeLightingEnabled && BarUiDynamicEdgeLightingEnabled;
	if (!edgeLightingEnabled)
	{
		// 总开关关闭时停止点光计算，基础灰边仍由绘制阶段保留。
		bool needSettlingFrame = frameEdgeLightingEnabled || frameLightingWasAnimating
			|| frameCursorLightIntensity > 0.0001F;
		frameEdgeLightingEnabled = false;
		framePrimaryLightAnchorInitialized = false;
		framePrimaryLightAnimating = false;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget = 0.0F;
		frameCursorLightAnimating = false;
		frameCursorInputAvailable = false;
		frameAnimationStateInitialized = false;
		frameDrawingPenColorAnimating = false;
		frameDrawingPenColorInitialized = false;
		frameDrawingModeTransitionAnimating = false;
		frameDrawingModeInitialized = false;
		frameLightingWasAnimating = false;
		return needSettlingFrame;
	}
	bool edgeLightingStateChanged = !frameEdgeLightingEnabled;
	frameEdgeLightingEnabled = true;

	BarBorderPrimaryAnchorEnum desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::MainButton;
	D2D1_POINT_2F desiredPrimaryLight = framePrimaryLight;
	bool primaryTargetAvailable = false;
	if (barUISetClass)
	{
		// 离开画笔模式后 GetPenColor 会回退到黑色；退出过渡必须保留最后一次有效画笔色。
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
			desiredDrawingPenColor = GetPenColor() & 0x00FFFFFF;
		switch (stateMode.StateModeSelect)
		{
		case StateModeSelectEnum::IdtPen:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Draw;
			break;
		case StateModeSelectEnum::IdtEraser:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Eraser;
			break;
		case StateModeSelectEnum::IdtSelection:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Select;
			break;
		default:
			break;
		}

		auto mainButtonIt = barUISetClass->superellipseMap.find(
			BarUISetSuperellipseEnum::MainButton);
		if (mainButtonIt != barUISetClass->superellipseMap.end() && mainButtonIt->second)
		{
			auto mainButton = mainButtonIt->second;
			desiredPrimaryLight = D2D1::Point2F(
				static_cast<FLOAT>(mainButton->x.val * zoom),
				static_cast<FLOAT>((mainButton->y.val + mainButton->h.val / 2.0) * zoom));
			primaryTargetAvailable = true;

			BarButtomPresetEnum anchorPreset = BarButtomPresetEnum::None;
			switch (desiredPrimaryAnchor)
			{
			case BarBorderPrimaryAnchorEnum::Select: anchorPreset = BarButtomPresetEnum::Select; break;
			case BarBorderPrimaryAnchorEnum::Draw: anchorPreset = BarButtomPresetEnum::Draw; break;
			case BarBorderPrimaryAnchorEnum::Eraser: anchorPreset = BarButtomPresetEnum::Eraser; break;
			default: break;
			}

			auto mainBarIt = barUISetClass->shapeMap.find(BarUISetShapeEnum::MainBar);
			BarButtomClass* anchorButton = anchorPreset == BarButtomPresetEnum::None
				? nullptr : barUISetClass->barButtomSet.preset[static_cast<int>(anchorPreset)];
			if (mainBarIt != barUISetClass->shapeMap.end() && mainBarIt->second && anchorButton)
			{
				auto mainBar = mainBarIt->second;
				double mainBarLeft = mainButton->x.val + mainBar->x.val - mainBar->w.val / 2.0;
				double mainBarTop = mainButton->y.val + mainBar->y.val - mainBar->h.val / 2.0;
				// 主光落在当前模式按钮的下边缘中心，控件布局动画时目标也随之更新。
				desiredPrimaryLight = D2D1::Point2F(
					static_cast<FLOAT>((mainBarLeft + anchorButton->buttom.x.val) * zoom),
					static_cast<FLOAT>((mainBarTop + anchorButton->buttom.y.val
						+ anchorButton->buttom.h.val / 2.0) * zoom));
			}
		}

		// 非穿透画笔模式下，仅由控件自己的光色策略决定是否采用画笔色。
		frameDrawingUsesPenColor =
			stateMode.StateModeSelect == StateModeSelectEnum::IdtPen && !penetrate.select;
	}

	unsigned long long cursorSerial = 0;
	bool cursorInputAvailable = false;
	if (barUISetClass)
	{
		lock_guard lock(barUISetClass->borderCursorLightMutex);
		frameCursorLight = barUISetClass->borderCursorLightPoint;
		cursorSerial = barUISetClass->borderCursorLightSerial;
		cursorInputAvailable = barUISetClass->borderCursorInputAvailable
			&& barUISetClass->borderCursorLightReady;
	}

	bool animationEnabled = BarUiAnimationEnabled;
	double animationSpeedRate = BarUiAnimationSpeedRate;
	if (!isfinite(animationSpeedRate)) animationSpeedRate = 1.0;
	animationSpeedRate = clamp(animationSpeedRate, 0.1, 5.0);
	if (!isfinite(animationDtSeconds) || animationDtSeconds < 0.0) animationDtSeconds = 0.0;
	animationDtSeconds = clamp(animationDtSeconds, 0.0, 0.05);
	double scaledDtSeconds = animationDtSeconds * animationSpeedRate;

	bool drawingModeTransitionStarted = false;
	double desiredPenColorBlend = frameDrawingUsesPenColor ? 1.0 : 0.0;
	if (!frameDrawingModeInitialized)
	{
		frameDrawingModeInitialized = true;
		frameDrawingPenColorBlend = desiredPenColorBlend;
		frameDrawingPenColorBlendStart = desiredPenColorBlend;
		frameDrawingPenColorBlendTarget = desiredPenColorBlend;
		frameDrawingLightOpacity = 1.0;
		frameDrawingLightOpacityStart = 1.0;
	}
	else if (frameDrawingPenColorBlendTarget != desiredPenColorBlend)
	{
		// 进入与退出绘制共用同一关键帧：先隐藏光影，在中点换色，再对称显示。
		frameDrawingPenColorBlendStart = frameDrawingPenColorBlend;
		frameDrawingPenColorBlendTarget = desiredPenColorBlend;
		frameDrawingLightOpacityStart = frameDrawingLightOpacity;
		frameDrawingModeTransitionElapsed = 0.0;
		frameDrawingModeTransitionAnimating = animationEnabled;
		drawingModeTransitionStarted = true;
	}

	if (!animationEnabled)
	{
		frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
		frameDrawingLightOpacity = 1.0;
		frameDrawingModeTransitionAnimating = false;
		frameDrawingModeTransitionElapsed = 0.0;
	}
	else if (frameDrawingModeTransitionAnimating)
	{
		double transitionDuration = BarUiDefaultOperationDur;
		if (!isfinite(transitionDuration) || transitionDuration <= 0.0)
		{
			frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
			frameDrawingLightOpacity = 1.0;
			frameDrawingModeTransitionAnimating = false;
		}
		else
		{
			frameDrawingModeTransitionElapsed += scaledDtSeconds;
			double progress = clamp(
				frameDrawingModeTransitionElapsed / transitionDuration, 0.0, 1.0);
			// 颜色变化压缩在中间 30% 时段，光影在正中完全透明，避免看见颜色互相覆盖。
			double colorProgress = ApplyBorderLightSmoothstep(
				clamp((progress - 0.35) / 0.30, 0.0, 1.0));
			frameDrawingPenColorBlend = frameDrawingPenColorBlendStart
				+ (frameDrawingPenColorBlendTarget - frameDrawingPenColorBlendStart)
				* colorProgress;
			if (progress < 0.5)
			{
				double fadeOutProgress = ApplyBorderLightSmoothstep(progress * 2.0);
				frameDrawingLightOpacity =
					frameDrawingLightOpacityStart * (1.0 - fadeOutProgress);
			}
			else
			{
				frameDrawingLightOpacity = ApplyBorderLightSmoothstep(
					(progress - 0.5) * 2.0);
			}
			if (progress >= 1.0)
			{
				frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
				frameDrawingLightOpacity = 1.0;
				frameDrawingModeTransitionAnimating = false;
			}
		}
	}

	bool penLightColorChanged = false;
	if (!frameDrawingPenColorInitialized)
	{
		frameDrawingPenColorInitialized = true;
		frameDrawingPenColorStart = desiredDrawingPenColor;
		frameDrawingPenColorTarget = desiredDrawingPenColor;
		frameDrawingPenColor = desiredDrawingPenColor;
	}
	else if ((frameDrawingPenColorTarget & 0x00FFFFFF) != desiredDrawingPenColor)
	{
		// 连续换色从当前视觉颜色重新起步，避免快速点击色块时发生跳变。
		frameDrawingPenColorStart = frameDrawingPenColor;
		frameDrawingPenColorTarget = desiredDrawingPenColor;
		frameDrawingPenColorElapsed = 0.0;
		frameDrawingPenColorAnimating = animationEnabled && frameDrawingUsesPenColor;
		penLightColorChanged = true;
	}

	if (!animationEnabled || !frameDrawingUsesPenColor)
	{
		frameDrawingPenColor = frameDrawingPenColorTarget;
		frameDrawingPenColorAnimating = false;
		frameDrawingPenColorElapsed = 0.0;
	}
	else if (frameDrawingPenColorAnimating)
	{
		double colorDuration = BarUiDefaultOperationDur;
		if (!isfinite(colorDuration) || colorDuration <= 0.0)
		{
			frameDrawingPenColor = frameDrawingPenColorTarget;
			frameDrawingPenColorAnimating = false;
		}
		else
		{
			frameDrawingPenColorElapsed += scaledDtSeconds;
			double progress = clamp(frameDrawingPenColorElapsed / colorDuration, 0.0, 1.0);
			double curvedProgress = BarUiApplyCurve(BarUiCurveEnum::EaseInOutCubic, progress);
			frameDrawingPenColor = MixBarUiColor(
				frameDrawingPenColorStart, frameDrawingPenColorTarget, curvedProgress);
			if (progress >= 1.0)
			{
				frameDrawingPenColor = frameDrawingPenColorTarget;
				frameDrawingPenColorAnimating = false;
			}
		}
	}

	bool primaryLightMoved = false;
	bool primaryStateChanged = false;
	if (primaryTargetAvailable)
	{
		auto PointsDiffer = [](D2D1_POINT_2F left, D2D1_POINT_2F right)
			{
				return abs(left.x - right.x) > 0.01F || abs(left.y - right.y) > 0.01F;
			};
		D2D1_POINT_2F previousPrimaryLight = framePrimaryLight;
		if (!framePrimaryLightAnchorInitialized)
		{
			framePrimaryLightAnchorInitialized = true;
			framePrimaryLightAnchor = desiredPrimaryAnchor;
			framePrimaryLightStart = desiredPrimaryLight;
			framePrimaryLightTarget = desiredPrimaryLight;
			framePrimaryLight = desiredPrimaryLight;
		}
		else
		{
			bool anchorChanged = framePrimaryLightAnchor != desiredPrimaryAnchor;
			if (anchorChanged)
			{
				framePrimaryLightAnchor = desiredPrimaryAnchor;
				framePrimaryLightStart = framePrimaryLight;
				framePrimaryLightMoveElapsed = 0.0;
				framePrimaryLightAnimating = animationEnabled;
				primaryStateChanged = true;
			}
			framePrimaryLightTarget = desiredPrimaryLight;

			if (!animationEnabled)
			{
				framePrimaryLight = desiredPrimaryLight;
				framePrimaryLightAnimating = false;
				framePrimaryLightMoveElapsed = 0.0;
			}
			else if (framePrimaryLightAnimating)
			{
				double moveDuration = BarUiDefaultOperationDur;
				if (!isfinite(moveDuration) || moveDuration <= 0.0)
				{
					framePrimaryLight = framePrimaryLightTarget;
					framePrimaryLightAnimating = false;
				}
				else
				{
					framePrimaryLightMoveElapsed += scaledDtSeconds;
					double progress = clamp(framePrimaryLightMoveElapsed / moveDuration, 0.0, 1.0);
					double curvedProgress = BarUiApplyCurve(
						BarUiCurveEnum::EaseInOutCubic, progress);
					framePrimaryLight = D2D1::Point2F(
						static_cast<FLOAT>(framePrimaryLightStart.x
							+ (framePrimaryLightTarget.x - framePrimaryLightStart.x) * curvedProgress),
						static_cast<FLOAT>(framePrimaryLightStart.y
							+ (framePrimaryLightTarget.y - framePrimaryLightStart.y) * curvedProgress));
					if (progress >= 1.0)
					{
						framePrimaryLight = framePrimaryLightTarget;
						framePrimaryLightAnimating = false;
					}
				}
			}
			else framePrimaryLight = desiredPrimaryLight;
		}
		primaryLightMoved = PointsDiffer(previousPrimaryLight, framePrimaryLight);
	}

	bool stateChanged = edgeLightingStateChanged || primaryStateChanged || penLightColorChanged
		|| drawingModeTransitionStarted;
	bool cursorFadeRestarted = false;
	bool cursorMoved = false;
	bool desiredCursorLightVisible = animationEnabled
		&& dynamicEdgeLightingEnabled && cursorInputAvailable;
	if (!frameAnimationStateInitialized)
	{
		frameAnimationStateInitialized = true;
		frameLastAnimationEnabled = animationEnabled;
		frameCursorInputAvailable = desiredCursorLightVisible;
		handledBorderCursorLightSerial = cursorSerial;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget =
			desiredCursorLightVisible ? static_cast<FLOAT>(BarBorderLightIntensity) : 0.0F;
		if (desiredCursorLightVisible)
		{
			frameCursorLightFadeElapsed = 0.0;
			frameCursorLightAnimating = true;
			cursorFadeRestarted = true;
		}
	}
	else
	{
		if (animationEnabled != frameLastAnimationEnabled)
		{
			stateChanged = true;
			frameLastAnimationEnabled = animationEnabled;
		}
		if (desiredCursorLightVisible != frameCursorInputAvailable)
		{
			// 显隐切换从当前强度续接，靠近、超时和重新进入时不会发生亮度跳变。
			frameCursorInputAvailable = desiredCursorLightVisible;
			stateChanged = true;
			handledBorderCursorLightSerial = cursorSerial;
			frameCursorLightIntensityStart = frameCursorLightIntensity;
			frameCursorLightIntensityTarget = desiredCursorLightVisible
				? static_cast<FLOAT>(BarBorderLightIntensity) : 0.0F;
			frameCursorLightFadeElapsed = 0.0;
			frameCursorLightAnimating = animationEnabled
				&& abs(frameCursorLightIntensityTarget - frameCursorLightIntensityStart) > 0.0001F;
			cursorFadeRestarted = frameCursorLightAnimating;
		}
	}

	if (!animationEnabled)
	{
		// 关闭动画后立即隐藏鼠标光，基础灰边和第一主光保持稳定。
		handledBorderCursorLightSerial = cursorSerial;
		frameCursorInputAvailable = false;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget = 0.0F;
		frameCursorLightAnimating = false;
		bool needSettlingFrame = frameLightingWasAnimating;
		frameLightingWasAnimating = false;
		return stateChanged || needSettlingFrame || primaryLightMoved;
	}

	if (cursorSerial != handledBorderCursorLightSerial)
	{
		handledBorderCursorLightSerial = cursorSerial;
		cursorMoved = desiredCursorLightVisible;
	}

	if (frameCursorLightAnimating)
	{
		if (!cursorFadeRestarted) frameCursorLightFadeElapsed += scaledDtSeconds;
		double progress = frameCursorLightFadeElapsed / BarBorderCursorFadeInDur;
		double curvedProgress = ApplyBorderLightSmoothstep(progress);
		frameCursorLightIntensity = static_cast<FLOAT>(
			frameCursorLightIntensityStart
			+ (frameCursorLightIntensityTarget - frameCursorLightIntensityStart)
			* curvedProgress);
		if (frameCursorLightFadeElapsed >= BarBorderCursorFadeInDur)
		{
			frameCursorLightIntensity = frameCursorLightIntensityTarget;
			frameCursorLightAnimating = false;
		}
	}
	else frameCursorLightIntensity = frameCursorLightIntensityTarget;
	frameCursorLightVisible = frameCursorLightIntensity > 0.0001F;

	// 时间过程结束后再绘制一帧最终状态，随后恢复原有静止等待。
	bool lightingAnimating = frameCursorLightAnimating || framePrimaryLightAnimating
		|| frameDrawingPenColorAnimating || frameDrawingModeTransitionAnimating;
	bool needSettlingFrame = frameLightingWasAnimating && !lightingAnimating;
	frameLightingWasAnimating = lightingAnimating;
	return lightingAnimating || needSettlingFrame || stateChanged || cursorMoved || primaryLightMoved;
}

ID2D1RadialGradientBrush* BarUIRendering::GetFrameGradientBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color, BarBorderLightSourceEnum lightSource)
{
	COLORREF rgb = color & 0x00FFFFFF;
	D2D1_POINT_2F center = framePrimaryLight;
	if (lightSource == BarBorderLightSourceEnum::Cursor) center = frameCursorLight;
	FLOAT radius = lightSource == BarBorderLightSourceEnum::Cursor
		? frameCursorLightRadius : frameLightRadius;
	if (radius <= 0.0F) return nullptr;

	for (auto& cache : frameGradientBrushCache)
	{
		if (cache.color == rgb && cache.lightSource == lightSource)
		{
			// 颜色停靠点长期复用，动态光位置与半径只更新画刷的轻量属性。
			cache.brush->SetCenter(center);
			cache.brush->SetGradientOriginOffset(D2D1::Point2F());
			cache.brush->SetRadiusX(radius);
			cache.brush->SetRadiusY(radius);
			return cache.brush.Get();
		}
	}

	D2D1_GRADIENT_STOP gradientStops[] =
	{
		{ 0.00F, Inkeys::Color::ConvertToD2dColor(rgb, 1.00) },
		{ 0.25F, Inkeys::Color::ConvertToD2dColor(rgb, 0.72) },
		{ 0.65F, Inkeys::Color::ConvertToD2dColor(rgb, 0.20) },
		{ 1.00F, Inkeys::Color::ConvertToD2dColor(rgb, 0.00) },
	};

	ComPtr<ID2D1GradientStopCollection> stopCollection;
	HRESULT hr = deviceContext->CreateGradientStopCollection(
		gradientStops, ARRAYSIZE(gradientStops), D2D1_GAMMA_2_2,
		D2D1_EXTEND_MODE_CLAMP, &stopCollection);
	if (SUCCEEDED(hr))
	{
		FrameGradientBrushCacheClass cache;
		cache.color = rgb;
		cache.lightSource = lightSource;
		hr = deviceContext->CreateRadialGradientBrush(
			D2D1::RadialGradientBrushProperties(
				center, D2D1::Point2F(), radius, radius),
			stopCollection.Get(), &cache.brush);
		if (SUCCEEDED(hr))
		{
			// 动画混色会产生短期颜色，限制缓存容量避免长期运行后无界增长。
			if (frameGradientBrushCache.size() >= 32)
				frameGradientBrushCache.erase(frameGradientBrushCache.begin());
			frameGradientBrushCache.emplace_back(move(cache));
			return frameGradientBrushCache.back().brush.Get();
		}
	}

	if (!frameGradientFailureLogged)
	{
		frameGradientFailureLogged = true;
		if (IDTLogger) IDTLogger->error(
			"[BarUIRendering::GetFrameGradientBrush] 创建边框点光渐变失败, hr=0x{:08X}",
			static_cast<unsigned int>(hr));
	}
	return nullptr;
}

ID2D1SolidColorBrush* BarUIRendering::GetFrameSolidColorBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color, double opacity)
{
	if (!deviceContext) return nullptr;
	D2D1_COLOR_F d2dColor = Inkeys::Color::ConvertToD2dColor(
		color, clamp(opacity, 0.0, 1.0));
	if (!frameSolidColorBrush)
	{
		if (FAILED(deviceContext->CreateSolidColorBrush(
			d2dColor, &frameSolidColorBrush)))
			return nullptr;
	}
	else
	{
		frameSolidColorBrush->SetColor(d2dColor);
		frameSolidColorBrush->SetOpacity(1.0F);
	}
	return frameSolidColorBrush.Get();
}

ID2D1LinearGradientBrush* BarUIRendering::GetThicknessPreviewGradientBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color,
	D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
	FLOAT leftOpacity)
{
	if (!deviceContext || thicknessPreviewGradientUnavailable) return nullptr;
	COLORREF rgb = color & 0x00FFFFFF;
	leftOpacity = round(clamp(leftOpacity, 0.0F, 1.0F) * 255.0F) / 255.0F;
	if (!thicknessPreviewGradientBrush
		|| !thicknessPreviewGradientColorInitialized
		|| thicknessPreviewGradientColor != rgb
		|| thicknessPreviewGradientLeftOpacity != leftOpacity)
	{
		D2D1_GRADIENT_STOP gradientStops[] =
		{
			{ 0.0F, Inkeys::Color::ConvertToD2dColor(rgb, leftOpacity) },
			{ 1.0F, Inkeys::Color::ConvertToD2dColor(rgb, 1.00) },
		};
		ComPtr<ID2D1GradientStopCollection> stopCollection;
		HRESULT hr = deviceContext->CreateGradientStopCollection(
			gradientStops, ARRAYSIZE(gradientStops), D2D1_GAMMA_2_2,
			D2D1_EXTEND_MODE_CLAMP, &stopCollection);
		if (SUCCEEDED(hr))
		{
			ComPtr<ID2D1LinearGradientBrush> brush;
			hr = deviceContext->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(startPoint, endPoint),
				stopCollection.Get(), &brush);
			if (SUCCEEDED(hr))
			{
				thicknessPreviewGradientBrush = move(brush);
				thicknessPreviewGradientColor = rgb;
				thicknessPreviewGradientLeftOpacity = leftOpacity;
				thicknessPreviewGradientColorInitialized = true;
			}
		}
		if (FAILED(hr))
		{
			thicknessPreviewGradientUnavailable = true;
			if (!thicknessPreviewGradientFailureLogged)
			{
				thicknessPreviewGradientFailureLogged = true;
				if (IDTLogger) IDTLogger->error(
					"[BarUIRendering::GetThicknessPreviewGradientBrush] 创建粗细预览渐变失败, hr=0x{:08X}",
					static_cast<unsigned int>(hr));
			}
			return nullptr;
		}
	}

	thicknessPreviewGradientBrush->SetStartPoint(startPoint);
	thicknessPreviewGradientBrush->SetEndPoint(endPoint);
	thicknessPreviewGradientBrush->SetOpacity(1.0F);
	return thicknessPreviewGradientBrush.Get();
}

ID2D1PathGeometry* BarUIRendering::GetThicknessPreviewPath(
	const array<D2D1_POINT_2F, 4>& points)
{
	if (!d2dFactory1 || thicknessPreviewPathUnavailable) return nullptr;
	bool pointsChanged = !thicknessPreviewPathInitialized;
	for (size_t i = 0; i < points.size() && !pointsChanged; ++i)
	{
		pointsChanged = abs(points[i].x - thicknessPreviewPathPoints[i].x) > 0.001F
			|| abs(points[i].y - thicknessPreviewPathPoints[i].y) > 0.001F;
	}
	if (!pointsChanged && thicknessPreviewPath) return thicknessPreviewPath.Get();

	ComPtr<ID2D1PathGeometry> path;
	HRESULT hr = d2dFactory1->CreatePathGeometry(&path);
	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1GeometrySink> sink;
		hr = path->Open(&sink);
		if (SUCCEEDED(hr))
		{
			sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_HOLLOW);
			D2D1_BEZIER_SEGMENT segment{ points[1], points[2], points[3] };
			sink->AddBezier(segment);
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			hr = sink->Close();
		}
	}
	if (FAILED(hr))
	{
		thicknessPreviewPathUnavailable = true;
		if (!thicknessPreviewPathFailureLogged)
		{
			thicknessPreviewPathFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetThicknessPreviewPath] 创建粗细预览路径失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	thicknessPreviewPath = move(path);
	thicknessPreviewPathPoints = points;
	thicknessPreviewPathInitialized = true;
	return thicknessPreviewPath.Get();
}

ID2D1StrokeStyle* BarUIRendering::GetThicknessPreviewStrokeStyle()
{
	if (thicknessPreviewStrokeStyle) return thicknessPreviewStrokeStyle.Get();
	if (!d2dFactory1 || thicknessPreviewPathUnavailable) return nullptr;
	D2D1_STROKE_STYLE_PROPERTIES properties = D2D1::StrokeStyleProperties(
		D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
		D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND,
		10.0F, D2D1_DASH_STYLE_SOLID, 0.0F);
	HRESULT hr = d2dFactory1->CreateStrokeStyle(
		properties, nullptr, 0, &thicknessPreviewStrokeStyle);
	if (FAILED(hr))
	{
		thicknessPreviewPathUnavailable = true;
		if (!thicknessPreviewPathFailureLogged)
		{
			thicknessPreviewPathFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetThicknessPreviewStrokeStyle] 创建圆头描边样式失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	return thicknessPreviewStrokeStyle.Get();
}

BarUIRendering::FrameDiffuseMaskCacheClass* BarUIRendering::GetRoundedRectDiffuseMask(
	ID2D1DeviceContext* deviceContext,
	const D2D1_ROUNDED_RECT& roundedRect, FLOAT strokeWidth)
{
	if (!deviceContext || strokeWidth <= 0.0F || !barUISetClass
		|| frameDiffuseMaskUnavailable) return nullptr;
	FLOAT standardDeviation = static_cast<FLOAT>(
		BarRenderingAttribute::pointLightDiffuseExtraWidth / 6.0
		* static_cast<double>(barUISetClass->barStyle.zoom));
	if (standardDeviation <= 0.0F) return nullptr;

	auto QuantizeQuarter = [](FLOAT value) -> int
		{
			return max(0, static_cast<int>(lround(static_cast<double>(value) * 4.0)));
		};
	int radiusXQuarter = QuantizeQuarter(roundedRect.radiusX);
	int radiusYQuarter = QuantizeQuarter(roundedRect.radiusY);
	int strokeWidthQuarter = max(1, QuantizeQuarter(strokeWidth));
	int standardDeviationQuarter = max(1, QuantizeQuarter(standardDeviation));
	for (auto& cache : frameDiffuseMaskCache)
	{
		if (cache.radiusXQuarter == radiusXQuarter
			&& cache.radiusYQuarter == radiusYQuarter
			&& cache.strokeWidthQuarter == strokeWidthQuarter
			&& cache.standardDeviationQuarter == standardDeviationQuarter)
			return &cache;
	}

	if (!frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		ComPtr<ID2D1Device> owningDevice;
		deviceContext->GetDevice(&owningDevice);
		HRESULT hr = owningDevice
			? owningDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &frameMaskDeviceContext)
			: E_POINTER;
		if (FAILED(hr))
		{
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundedRectDiffuseMask] 创建遮罩缓存 DeviceContext 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameGaussianBlurEffect
		&& frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		HRESULT hr = frameMaskDeviceContext->CreateEffect(
			CLSID_D2D1GaussianBlur, &frameGaussianBlurEffect);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
				D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
		if (FAILED(hr))
		{
			frameGaussianBlurEffect.Reset();
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundedRectDiffuseMask] 创建 Gaussian Blur Effect 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameMaskDeviceContext || !frameGaussianBlurEffect) return nullptr;

	FrameDiffuseMaskCacheClass cache;
	cache.radiusXQuarter = radiusXQuarter;
	cache.radiusYQuarter = radiusYQuarter;
	cache.strokeWidthQuarter = strokeWidthQuarter;
	cache.standardDeviationQuarter = standardDeviationQuarter;
	cache.radiusX = static_cast<FLOAT>(radiusXQuarter) / 4.0F;
	cache.radiusY = static_cast<FLOAT>(radiusYQuarter) / 4.0F;
	FLOAT cachedStrokeWidth = static_cast<FLOAT>(strokeWidthQuarter) / 4.0F;
	FLOAT cachedStandardDeviation =
		static_cast<FLOAT>(standardDeviationQuarter) / 4.0F;
	cache.padding = ceilf(cachedStandardDeviation * 3.0F
		+ cachedStrokeWidth * 0.5F + 1.0F);
	cache.size = D2D1::SizeF(
		cache.padding * 2.0F + cache.radiusX * 2.0F + 1.0F,
		cache.padding * 2.0F + cache.radiusY * 2.0F + 1.0F);

	D2D1_SIZE_U pixelSize = D2D1::SizeU(
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.width))),
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.height))));
	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(
			DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0F, 96.0F);
	ComPtr<ID2D1Bitmap1> sourceBitmap;
	ComPtr<ID2D1Bitmap1> outputBitmap;
	HRESULT hr = frameMaskDeviceContext->CreateBitmap(
		pixelSize, nullptr, 0, bitmapProperties, &sourceBitmap);
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateBitmap(
			pixelSize, nullptr, 0, bitmapProperties, &outputBitmap);
	ComPtr<ID2D1SolidColorBrush> sourceBrush;
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White), &sourceBrush);
	if (SUCCEEDED(hr))
		hr = frameGaussianBlurEffect->SetValue(
			D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
			cachedStandardDeviation);

	if (SUCCEEDED(hr))
	{
		D2D1_COLOR_F transparent = D2D1::ColorF(0.0F, 0.0F);
		D2D1_ROUNDED_RECT localRoundedRect = D2D1::RoundedRect(
			D2D1::RectF(
				cache.padding, cache.padding,
				cache.padding + cache.radiusX * 2.0F + 1.0F,
				cache.padding + cache.radiusY * 2.0F + 1.0F),
			cache.radiusX, cache.radiusY);

		// 缓存上下文分两次提交，避免同一 BeginDraw 内把刚写完的 Target 当作 Effect 输入。
		frameMaskDeviceContext->SetTarget(sourceBitmap.Get());
		frameMaskDeviceContext->BeginDraw();
		frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
		frameMaskDeviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
		frameMaskDeviceContext->Clear(&transparent);
		frameMaskDeviceContext->DrawRoundedRectangle(
			&localRoundedRect, sourceBrush.Get(), cachedStrokeWidth);
		hr = frameMaskDeviceContext->EndDraw();
		frameMaskDeviceContext->SetTarget(nullptr);

		if (SUCCEEDED(hr))
		{
			frameGaussianBlurEffect->SetInput(0, sourceBitmap.Get());
			frameMaskDeviceContext->SetTarget(outputBitmap.Get());
			frameMaskDeviceContext->BeginDraw();
			frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
			frameMaskDeviceContext->Clear(&transparent);
			frameMaskDeviceContext->DrawImage(frameGaussianBlurEffect.Get());
			hr = frameMaskDeviceContext->EndDraw();
			frameMaskDeviceContext->SetTarget(nullptr);
			frameGaussianBlurEffect->SetInput(0, nullptr);
		}
	}

	if (FAILED(hr))
	{
		frameDiffuseMaskUnavailable = true;
		if (!frameDiffuseMaskFailureLogged)
		{
			frameDiffuseMaskFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundedRectDiffuseMask] 创建预模糊遮罩失败，本设备停用柔光遮罩, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	cache.bitmap = move(outputBitmap);
	if (frameDiffuseMaskCache.size() >= 24)
		frameDiffuseMaskCache.erase(frameDiffuseMaskCache.begin());
	frameDiffuseMaskCache.emplace_back(move(cache));
	frameDiffuseMaskCreatedThisFrame = true;
	return &frameDiffuseMaskCache.back();
}

void BarUIRendering::DrawRoundedRectDiffuseMask(ID2D1DeviceContext* deviceContext,
	const FrameDiffuseMaskCacheClass& mask,
	const D2D1_ROUNDED_RECT& roundedRect,
	ID2D1RadialGradientBrush* brush, FLOAT opacity)
{
	if (!deviceContext || !mask.bitmap || !brush || opacity <= 0.0F) return;
	brush->SetOpacity(clamp(opacity, 0.0F, 1.0F));

	FLOAT destinationLeft = roundedRect.rect.left - mask.padding;
	FLOAT destinationTop = roundedRect.rect.top - mask.padding;
	FLOAT destinationRight = roundedRect.rect.right + mask.padding;
	FLOAT destinationBottom = roundedRect.rect.bottom + mask.padding;
	FLOAT destinationRadiusX = max(0.0F, roundedRect.radiusX);
	FLOAT destinationRadiusY = max(0.0F, roundedRect.radiusY);
	FLOAT destinationMiddleLeft = min(
		roundedRect.rect.left + destinationRadiusX,
		(roundedRect.rect.left + roundedRect.rect.right) * 0.5F);
	FLOAT destinationMiddleRight = max(
		roundedRect.rect.right - destinationRadiusX, destinationMiddleLeft);
	FLOAT destinationMiddleTop = min(
		roundedRect.rect.top + destinationRadiusY,
		(roundedRect.rect.top + roundedRect.rect.bottom) * 0.5F);
	FLOAT destinationMiddleBottom = max(
		roundedRect.rect.bottom - destinationRadiusY, destinationMiddleTop);

	D2D1_ANTIALIAS_MODE originalAntialiasMode = deviceContext->GetAntialiasMode();
	deviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	auto DrawSlices = [&](const FLOAT* sourceX, const FLOAT* sourceY,
		const FLOAT* destinationX, const FLOAT* destinationY, int segmentCount)
	{
		for (int y = 0; y < segmentCount; y++)
		{
			for (int x = 0; x < segmentCount; x++)
			{
				if (destinationX[x + 1] <= destinationX[x]
					|| destinationY[y + 1] <= destinationY[y]) continue;
				D2D1_RECT_F destinationRect = D2D1::RectF(
					destinationX[x], destinationY[y],
					destinationX[x + 1], destinationY[y + 1]);
				D2D1_RECT_F sourceRect = D2D1::RectF(
					sourceX[x], sourceY[y],
					sourceX[x + 1], sourceY[y + 1]);
				deviceContext->FillOpacityMask(
					mask.bitmap.Get(), brush, &destinationRect, &sourceRect);
			}
		}
	};
	bool geometryScaled = abs(destinationRadiusX - mask.radiusX) > 0.001F
		|| abs(destinationRadiusY - mask.radiusY) > 0.001F;
	if (!geometryScaled)
	{
		const FLOAT sourceX[] = { 0.0F,
			mask.padding + mask.radiusX,
			mask.padding + mask.radiusX + 1.0F, mask.size.width };
		const FLOAT sourceY[] = { 0.0F,
			mask.padding + mask.radiusY,
			mask.padding + mask.radiusY + 1.0F, mask.size.height };
		const FLOAT destinationX[] = { destinationLeft,
			destinationMiddleLeft, destinationMiddleRight, destinationRight };
		const FLOAT destinationY[] = { destinationTop,
			destinationMiddleTop, destinationMiddleBottom, destinationBottom };
		DrawSlices(sourceX, sourceY, destinationX, destinationY, 3);
	}
	else
	{
		// 动画缩放时单独保留 Gaussian 外扩段，避免柔光宽度随圆角一起压扁。
		const FLOAT sourceX[] = { 0.0F, mask.padding,
			mask.padding + mask.radiusX,
			mask.padding + mask.radiusX + 1.0F,
			mask.padding + mask.radiusX * 2.0F + 1.0F, mask.size.width };
		const FLOAT sourceY[] = { 0.0F, mask.padding,
			mask.padding + mask.radiusY,
			mask.padding + mask.radiusY + 1.0F,
			mask.padding + mask.radiusY * 2.0F + 1.0F, mask.size.height };
		const FLOAT destinationX[] = { destinationLeft, roundedRect.rect.left,
			destinationMiddleLeft, destinationMiddleRight,
			roundedRect.rect.right, destinationRight };
		const FLOAT destinationY[] = { destinationTop, roundedRect.rect.top,
			destinationMiddleTop, destinationMiddleBottom,
			roundedRect.rect.bottom, destinationBottom };
		DrawSlices(sourceX, sourceY, destinationX, destinationY, 5);
	}
	deviceContext->SetAntialiasMode(originalAntialiasMode);
}

BarUIRendering::FrameGeometryDiffuseMaskCacheClass* BarUIRendering::GetGeometryDiffuseMask(
	ID2D1DeviceContext* deviceContext, ID2D1Geometry* geometry,
	FLOAT strokeWidth, int geometryVariantQuarter)
{
	if (!deviceContext || !geometry || strokeWidth <= 0.0F || !barUISetClass)
		return nullptr;
	if (frameDiffuseMaskUnavailable) return nullptr;
	D2D1_RECT_F geometryBounds{};
	HRESULT hr = geometry->GetBounds(nullptr, &geometryBounds);
	if (FAILED(hr)) return nullptr;
	FLOAT width = geometryBounds.right - geometryBounds.left;
	FLOAT height = geometryBounds.bottom - geometryBounds.top;
	if (width <= 0.0F || height <= 0.0F) return nullptr;

	FLOAT standardDeviation = static_cast<FLOAT>(
		BarRenderingAttribute::pointLightDiffuseExtraWidth / 6.0
		* static_cast<double>(barUISetClass->barStyle.zoom));
	auto QuantizeQuarter = [](FLOAT value) -> int
		{
			return max(0, static_cast<int>(lround(static_cast<double>(value) * 4.0)));
		};
	int widthQuarter = max(1, QuantizeQuarter(width));
	int heightQuarter = max(1, QuantizeQuarter(height));
	int strokeWidthQuarter = max(1, QuantizeQuarter(strokeWidth));
	int standardDeviationQuarter = max(1, QuantizeQuarter(standardDeviation));
	for (auto& cache : frameGeometryDiffuseMaskCache)
	{
		if (cache.widthQuarter == widthQuarter
			&& cache.heightQuarter == heightQuarter
			&& cache.geometryVariantQuarter == geometryVariantQuarter
			&& cache.strokeWidthQuarter == strokeWidthQuarter
			&& cache.standardDeviationQuarter == standardDeviationQuarter)
			return &cache;
	}

	if (!frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		ComPtr<ID2D1Device> owningDevice;
		deviceContext->GetDevice(&owningDevice);
		hr = owningDevice
			? owningDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &frameMaskDeviceContext)
			: E_POINTER;
		if (FAILED(hr))
		{
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetGeometryDiffuseMask] 创建遮罩缓存 DeviceContext 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameGaussianBlurEffect
		&& frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		hr = frameMaskDeviceContext->CreateEffect(
			CLSID_D2D1GaussianBlur, &frameGaussianBlurEffect);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
				D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
		if (FAILED(hr))
		{
			frameGaussianBlurEffect.Reset();
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetGeometryDiffuseMask] 创建 Gaussian Blur Effect 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameMaskDeviceContext || !frameGaussianBlurEffect) return nullptr;

	FrameGeometryDiffuseMaskCacheClass cache;
	cache.widthQuarter = widthQuarter;
	cache.heightQuarter = heightQuarter;
	cache.geometryVariantQuarter = geometryVariantQuarter;
	cache.strokeWidthQuarter = strokeWidthQuarter;
	cache.standardDeviationQuarter = standardDeviationQuarter;
	FLOAT cachedStrokeWidth = static_cast<FLOAT>(strokeWidthQuarter) / 4.0F;
	FLOAT cachedStandardDeviation =
		static_cast<FLOAT>(standardDeviationQuarter) / 4.0F;
	cache.padding = ceilf(cachedStandardDeviation * 3.0F
		+ cachedStrokeWidth * 0.5F + 1.0F);
	cache.size = D2D1::SizeF(
		width + cache.padding * 2.0F,
		height + cache.padding * 2.0F);

	D2D1_SIZE_U pixelSize = D2D1::SizeU(
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.width))),
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.height))));
	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(
			DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0F, 96.0F);
	ComPtr<ID2D1Bitmap1> sourceBitmap;
	ComPtr<ID2D1Bitmap1> outputBitmap;
	hr = frameMaskDeviceContext->CreateBitmap(
		pixelSize, nullptr, 0, bitmapProperties, &sourceBitmap);
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateBitmap(
			pixelSize, nullptr, 0, bitmapProperties, &outputBitmap);
	ComPtr<ID2D1SolidColorBrush> sourceBrush;
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White), &sourceBrush);
	if (SUCCEEDED(hr))
		hr = frameGaussianBlurEffect->SetValue(
			D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
			cachedStandardDeviation);

	if (SUCCEEDED(hr))
	{
		D2D1_COLOR_F transparent = D2D1::ColorF(0.0F, 0.0F);

		// 缓存上下文分两次提交，避免同一 BeginDraw 内把刚写完的 Target 当作 Effect 输入。
		frameMaskDeviceContext->SetTarget(sourceBitmap.Get());
		frameMaskDeviceContext->BeginDraw();
		frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(
			cache.padding - geometryBounds.left,
			cache.padding - geometryBounds.top));
		frameMaskDeviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
		frameMaskDeviceContext->Clear(&transparent);
		frameMaskDeviceContext->DrawGeometry(
			geometry, sourceBrush.Get(), cachedStrokeWidth);
		hr = frameMaskDeviceContext->EndDraw();
		frameMaskDeviceContext->SetTarget(nullptr);

		if (SUCCEEDED(hr))
		{
			frameGaussianBlurEffect->SetInput(0, sourceBitmap.Get());
			frameMaskDeviceContext->SetTarget(outputBitmap.Get());
			frameMaskDeviceContext->BeginDraw();
			frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
			frameMaskDeviceContext->Clear(&transparent);
			frameMaskDeviceContext->DrawImage(frameGaussianBlurEffect.Get());
			hr = frameMaskDeviceContext->EndDraw();
			frameMaskDeviceContext->SetTarget(nullptr);
			frameGaussianBlurEffect->SetInput(0, nullptr);
		}
	}
	if (FAILED(hr))
	{
		frameDiffuseMaskUnavailable = true;
		if (!frameDiffuseMaskFailureLogged)
		{
			frameDiffuseMaskFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetGeometryDiffuseMask] 创建几何预模糊遮罩失败，本设备停用柔光遮罩, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	cache.bitmap = move(outputBitmap);
	if (frameGeometryDiffuseMaskCache.size() >= 24)
		frameGeometryDiffuseMaskCache.erase(frameGeometryDiffuseMaskCache.begin());
	frameGeometryDiffuseMaskCache.emplace_back(move(cache));
	frameDiffuseMaskCreatedThisFrame = true;
	return &frameGeometryDiffuseMaskCache.back();
}

void BarUIRendering::DrawGeometryDiffuseMask(ID2D1DeviceContext* deviceContext,
	const FrameGeometryDiffuseMaskCacheClass& mask,
	const D2D1_RECT_F& geometryBounds,
	ID2D1RadialGradientBrush* brush, FLOAT opacity)
{
	if (!deviceContext || !mask.bitmap || !brush || opacity <= 0.0F) return;
	D2D1_RECT_F destinationRect = D2D1::RectF(
		geometryBounds.left - mask.padding,
		geometryBounds.top - mask.padding,
		geometryBounds.right + mask.padding,
		geometryBounds.bottom + mask.padding);
	D2D1_RECT_F sourceRect = D2D1::RectF(
		0.0F, 0.0F, mask.size.width, mask.size.height);
	brush->SetOpacity(clamp(opacity, 0.0F, 1.0F));
	D2D1_ANTIALIAS_MODE originalAntialiasMode = deviceContext->GetAntialiasMode();
	deviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	deviceContext->FillOpacityMask(
		mask.bitmap.Get(), brush, &destinationRect, &sourceRect);
	deviceContext->SetAntialiasMode(originalAntialiasMode);
}

bool BarUIRendering::DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
	BarUiFrameLightColorEnum frameLightColor,
	bool primaryLightEnabled, double cursorLightIntensityScale,
	double baseFramePct, double lightPct, FLOAT strokeWidth,
	const D2D1_ROUNDED_RECT* roundedRect,
	ID2D1Geometry* geometry, int geometryVariantQuarter)
{
	if (!deviceContext || (!roundedRect && !geometry) || strokeWidth <= 0.0F || frameLightRadius <= 0.0F)
		return false;

	FLOAT baseOpacity = static_cast<FLOAT>(clamp(baseFramePct, 0.0, 1.0));
	FLOAT lightOpacity = static_cast<FLOAT>(clamp(lightPct, 0.0, 1.0));
	if (baseOpacity <= 0.0F && lightOpacity <= 0.0F) return true;

	bool useDrawingLightTransition =
		frameLightColor == BarUiFrameLightColorEnum::PenWhenDrawing;
	double penColorBlend = useDrawingLightTransition
		? clamp(frameDrawingPenColorBlend, 0.0, 1.0) : 0.0;
	if (useDrawingLightTransition)
		lightOpacity *= static_cast<FLOAT>(
			clamp(frameDrawingLightOpacity, 0.0, 1.0));
	COLORREF lightColor = color;
	if (penColorBlend > 0.0)
		lightColor = MixBarUiColor(color, frameDrawingPenColor, penColorBlend);
	FLOAT diffuseOpacity = static_cast<FLOAT>(
		BarBorderFrameDiffuseOpacity
		+ (BarBorderPenDiffuseOpacity - BarBorderFrameDiffuseOpacity) * penColorBlend);
	ID2D1RadialGradientBrush* primaryBrush = nullptr;
	ID2D1RadialGradientBrush* cursorBrush = nullptr;
	FLOAT cursorLightIntensity = frameCursorLightIntensity
		* static_cast<FLOAT>(clamp(cursorLightIntensityScale, 0.0, 1.0));
	bool edgeLightingEnabled = BarUiEdgeLightingEnabled;
	D2D1_RECT_F lightBounds{};
	if (roundedRect) lightBounds = roundedRect->rect;
	else if (geometry && FAILED(geometry->GetBounds(nullptr, &lightBounds)))
		return false;
	FLOAT lightBoundsOutset = strokeWidth
		+ static_cast<FLOAT>(
			BarRenderingAttribute::pointLightDiffuseExtraWidth
			* static_cast<double>(barUISetClass->barStyle.zoom));
	lightBounds.left -= lightBoundsOutset;
	lightBounds.top -= lightBoundsOutset;
	lightBounds.right += lightBoundsOutset;
	lightBounds.bottom += lightBoundsOutset;
	auto LightIntersectsBounds = [&](D2D1_POINT_2F point, FLOAT radius) -> bool
		{
			FLOAT nearestX = clamp(point.x, lightBounds.left, lightBounds.right);
			FLOAT nearestY = clamp(point.y, lightBounds.top, lightBounds.bottom);
			FLOAT deltaX = point.x - nearestX;
			FLOAT deltaY = point.y - nearestY;
			return deltaX * deltaX + deltaY * deltaY <= radius * radius;
		};
	bool drawPrimaryLight = edgeLightingEnabled
		&& lightOpacity > 0.0F && primaryLightEnabled
		&& LightIntersectsBounds(framePrimaryLight, frameLightRadius);
	bool drawCursorLight = edgeLightingEnabled
		&& lightOpacity > 0.0F && frameCursorLightVisible
		&& cursorLightIntensity > 0.0F
		&& LightIntersectsBounds(frameCursorLight, frameCursorLightRadius);
	if (drawPrimaryLight)
		primaryBrush = GetFrameGradientBrush(
			deviceContext, lightColor, BarBorderLightSourceEnum::Primary);
	if (drawCursorLight)
		cursorBrush = GetFrameGradientBrush(
			deviceContext, lightColor, BarBorderLightSourceEnum::Cursor);
	if ((drawPrimaryLight && !primaryBrush) || (drawCursorLight && !cursorBrush)) return false;

	ID2D1SolidColorBrush* baseFrameBrush = nullptr;
	if (baseOpacity > 0.0F)
	{
		baseFrameBrush = GetFrameSolidColorBrush(deviceContext, color, baseOpacity);
		if (!baseFrameBrush) return false;
	}

	auto DrawLightPass = [&](ID2D1RadialGradientBrush* brush, FLOAT intensity, FLOAT width)
		{
			if (!brush || intensity <= 0.0F) return;
			brush->SetOpacity(clamp(lightOpacity * intensity, 0.0F, 1.0F));
			if (roundedRect) deviceContext->DrawRoundedRectangle(roundedRect, brush, width);
			else deviceContext->DrawGeometry(geometry, brush, width);
		};
	deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	// 点光范围之外仍完整保留原边框，光源只在基础灰边上增加强调。
	if (baseFrameBrush)
	{
		if (roundedRect) deviceContext->DrawRoundedRectangle(roundedRect, baseFrameBrush, strokeWidth);
		else deviceContext->DrawGeometry(geometry, baseFrameBrush, strokeWidth);
	}

	if (drawPrimaryLight || drawCursorLight)
	{
		FLOAT diffuseSourceOpacity = static_cast<FLOAT>(clamp(
			diffuseOpacity / BarBorderGaussianCenterCoverage, 0.0, 1.0));
		auto CompositeOpacity = [](FLOAT opacity) -> FLOAT
			{
				opacity = clamp(opacity, 0.0F, 1.0F);
				return 1.0F - static_cast<FLOAT>(pow(
					1.0F - opacity, BarBorderDiffuseCompositePasses));
			};
		if (roundedRect)
		{
			D2D1_ROUNDED_RECT maskRoundedRect = *roundedRect;
			FLOAT maskStrokeWidth = strokeWidth;
			if (isfinite(frameDiffuseMaskGeometryScale)
				&& frameDiffuseMaskGeometryScale > 0.0)
			{
				// 等比动画只改变落点九宫格，缓存仍使用稳定的完整圆角和描边。
				maskRoundedRect.radiusX *=
					static_cast<FLOAT>(frameDiffuseMaskGeometryScale);
				maskRoundedRect.radiusY *=
					static_cast<FLOAT>(frameDiffuseMaskGeometryScale);
				maskStrokeWidth *=
					static_cast<FLOAT>(frameDiffuseMaskGeometryScale);
			}
			FrameDiffuseMaskCacheClass* diffuseMask =
				GetRoundedRectDiffuseMask(
					deviceContext, maskRoundedRect, maskStrokeWidth);
			if (diffuseMask)
			{
				// 模糊后的几何遮罩跨帧复用，光源颜色和位置仍由实时径向画刷决定。
				if (drawPrimaryLight)
				{
					DrawRoundedRectDiffuseMask(deviceContext, *diffuseMask,
						*roundedRect, primaryBrush,
						CompositeOpacity(lightOpacity * diffuseSourceOpacity));
				}
				if (drawCursorLight)
				{
					DrawRoundedRectDiffuseMask(deviceContext, *diffuseMask,
						*roundedRect, cursorBrush,
						CompositeOpacity(lightOpacity * cursorLightIntensity
							* diffuseSourceOpacity));
				}
			}
		}
		else if (geometry)
		{
			D2D1_RECT_F geometryBounds{};
			if (SUCCEEDED(geometry->GetBounds(nullptr, &geometryBounds)))
			{
				FrameGeometryDiffuseMaskCacheClass* diffuseMask =
					GetGeometryDiffuseMask(deviceContext, geometry,
						strokeWidth, geometryVariantQuarter);
				if (diffuseMask)
				{
					if (drawPrimaryLight)
					{
						DrawGeometryDiffuseMask(deviceContext, *diffuseMask,
							geometryBounds, primaryBrush,
							CompositeOpacity(lightOpacity * diffuseSourceOpacity));
					}
					if (drawCursorLight)
					{
						DrawGeometryDiffuseMask(deviceContext, *diffuseMask,
							geometryBounds, cursorBrush,
							CompositeOpacity(lightOpacity * cursorLightIntensity
								* diffuseSourceOpacity));
					}
				}
			}
		}

	}
	DrawLightPass(primaryBrush, static_cast<FLOAT>(BarBorderLightIntensity), strokeWidth);
	DrawLightPass(cursorBrush, cursorLightIntensity, strokeWidth);
	return true;
}

bool BarUIRendering::Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (shape.enable.val == false) return false;
	if (!shape.fill.has_value() && !shape.frame.has_value()) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (shape.w.val <= 0 || shape.h.val <= 0) return false;
	double frameLightPct = shape.frameLightPct.has_value()
		? clamp(static_cast<double>(shape.frameLightPct.value().val), 0.0, 1.0) : 0.0;
	if (shape.pct.val <= 0.0 && frameLightPct <= 0.0) return false;

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
		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, RGB(0, 0, 0), 0.0);
		if (!fillBrush) return false;
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillRoundedRectangle(&roundedRect, fillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}
	// 渲染到 DC
	{
		// 渲染填充
		if (shape.fill.has_value() && tarPct > 0.0)
		{
			COLORREF fill = shape.fill.value().val;
			ID2D1SolidColorBrush* fillBrush =
				GetFrameSolidColorBrush(deviceContext, fill, tarPct);
			if (!fillBrush) return false;
			deviceContext->FillRoundedRectangle(&roundedRect, fillBrush);
		}
		// 渲染边框
		if (shape.frame.has_value())
		{
			COLORREF frame = shape.frame.value().val;
			double tarFramePct = tarPct;
			if (shape.framePct.has_value()) tarFramePct = shape.framePct.value().val;
			double tarFrameLightPct = shape.frameLightPct.has_value()
				? frameLightPct : tarFramePct;
			if (!shape.frameLightPct.has_value()
				&& shape.frameLightOpacitySource == BarUiFrameLightOpacitySourceEnum::ObjectPct)
				tarFrameLightPct = tarPct;

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			bool shouldDraw = true;
			if (shape.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(shape.ft.value().val * tarZoom);
				shouldDraw = strokeWidth > 0.0F;
			}
			if (shouldDraw)
			{
				bool pointLightDrawn = shape.frameRendering == BarUiFrameRenderingEnum::PointLight
					&& DrawPointLightFrame(deviceContext, frame, shape.frameLightColor,
						shape.framePrimaryLightEnabled, shape.frameCursorLightIntensityScale,
						tarFramePct, tarFrameLightPct,
						strokeWidth, &roundedRect, nullptr);
				if (!pointLightDrawn)
				{
					ID2D1SolidColorBrush* borderBrush =
						GetFrameSolidColorBrush(deviceContext, frame, tarFramePct);
					if (!borderBrush) return false;
					deviceContext->DrawRoundedRectangle(
						&roundedRect, borderBrush, strokeWidth);
				}
			}
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
		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, RGB(0, 0, 0), 0.0);
		if (!fillBrush) return false;
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillGeometry(geometry.Get(), fillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}

	// 渲染到 DC
	{
		// 渲染填充
		if (superellipse.fill.has_value())
		{
			COLORREF fill = superellipse.fill.value().val;
			ID2D1SolidColorBrush* fillBrush =
				GetFrameSolidColorBrush(deviceContext, fill, tarPct);
			if (!fillBrush) return false;
			deviceContext->FillGeometry(geometry.Get(), fillBrush);
		}
		// 渲染边框
		if (superellipse.frame.has_value())
		{
			COLORREF frame = superellipse.frame.value().val;
			double tarFramePct = tarPct;
			if (superellipse.framePct.has_value()) tarFramePct = superellipse.framePct.value().val;
			double tarFrameLightPct = tarFramePct;
			if (superellipse.frameLightOpacitySource == BarUiFrameLightOpacitySourceEnum::ObjectPct)
				tarFrameLightPct = tarPct;

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			bool shouldDraw = true;
			if (superellipse.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(superellipse.ft.value().val * tarZoom);
				shouldDraw = strokeWidth > 0.0F;
			}
			if (shouldDraw)
			{
				bool pointLightDrawn = superellipse.frameRendering == BarUiFrameRenderingEnum::PointLight
					&& DrawPointLightFrame(deviceContext, frame, superellipse.frameLightColor,
						superellipse.framePrimaryLightEnabled,
						superellipse.frameCursorLightIntensityScale,
						tarFramePct, tarFrameLightPct,
						strokeWidth, nullptr, geometry.Get(),
						static_cast<int>(lround(tarN * 4.0)));
				if (!pointLightDrawn)
				{
					ID2D1SolidColorBrush* borderBrush =
						GetFrameSolidColorBrush(deviceContext, frame, tarFramePct);
					if (!borderBrush) return false;
					deviceContext->DrawGeometry(geometry.Get(), borderBrush, strokeWidth);
				}
			}
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
	double contentScale = svg.contentScale;
	double contentPct = svg.contentPct;
	if (!isfinite(contentScale) || contentScale <= 0.0) return false;
	if (!isfinite(contentPct) || svg.pct.val * contentPct <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
	double baseX = inh.x * tarZoom;
	double baseY = inh.y * tarZoom;
	double baseW = svg.w.val * tarZoom;
	double baseH = svg.h.val * tarZoom;
	// 内容倍率只改变目标矩形，并围绕原中心缩放；缓存继续使用基础尺寸。
	double tarW = baseW * contentScale;
	double tarH = baseH * contentScale;
	double tarX = baseX + (baseW - tarW) / 2.0;
	double tarY = baseY + (baseH - tarH) / 2.0;
	double tarPct = clamp(static_cast<double>(svg.pct.val) * contentPct, 0.0, 1.0);

	// 获取绘制缓存
	ComPtr<ID2D1Bitmap> d2dBitmap;
	{
		bool needUpdate = false;
		if (svg.cW != baseW || svg.cH != baseH) needUpdate = true;
		if (svg.color1.has_value() && svg.cColor1 != svg.color1.value().val) needUpdate = true;
		if (svg.color2.has_value() && svg.cColor2 != svg.color2.value().val) needUpdate = true;

		// TODO 优化：可选动画过程中不更新缓存
		if (needUpdate || !svg.cacheBitmap)
		{
			if (!svg.CacheBitmap(deviceContext, baseW, baseH))
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

		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, color, tarPct);
		if (!fillBrush) return false;

		deviceContext->DrawTextW(
			tarContent.c_str(),
			wcslen(tarContent.c_str()),
			textFormat,
			layoutRect,
			fillBrush,
			D2D1_DRAW_TEXT_OPTIONS_CLIP
		);
	}

	return true;
}

D2D1_SIZE_F BarUIRendering::MeasureText(
	const wstring& content, double fontSize, DWRITE_FONT_WEIGHT fontWeight)
{
	D2D1_SIZE_F result = D2D1::SizeF();
	if (content.empty() || !dWriteFactory1 || !barUISetClass
		|| !barUISetClass->barMedia.formatCache || fontSize <= 0.0)
		return result;

	IDWriteTextFormat* textFormat =
		barUISetClass->barMedia.formatCache->GetFormat(
			L"HarmonyOS Sans SC", static_cast<FLOAT>(fontSize),
			dWriteFontCollection.Get(),
			fontWeight, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, L"zh-cn",
			DWRITE_TEXT_ALIGNMENT_LEADING,
			DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	if (!textFormat) return result;

	ComPtr<IDWriteTextLayout> textLayout;
	HRESULT hr = dWriteFactory1->CreateTextLayout(
		content.c_str(), static_cast<UINT32>(content.size()), textFormat,
		4096.0F, 4096.0F, &textLayout);
	if (SUCCEEDED(hr))
	{
		textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		DWRITE_TEXT_METRICS metrics{};
		hr = textLayout->GetMetrics(&metrics);
		if (SUCCEEDED(hr))
		{
			result.width = ceil(metrics.widthIncludingTrailingWhitespace);
			result.height = ceil(metrics.height);
			return result;
		}
	}

	// DWrite 测量失败时保留可读区域，不能让提示框退化为零尺寸。
	size_t maxLineLength = 0;
	size_t currentLineLength = 0;
	size_t lineCount = 1;
	for (wchar_t ch : content)
	{
		if (ch == L'\n')
		{
			maxLineLength = max(maxLineLength, currentLineLength);
			currentLineLength = 0;
			++lineCount;
		}
		else ++currentLineLength;
	}
	maxLineLength = max(maxLineLength, currentLineLength);
	result.width = static_cast<FLOAT>(maxLineLength * fontSize);
	result.height = static_cast<FLOAT>(lineCount * fontSize * 1.4);
	return result;
}

// UI 总集

void BarUISetClass::CloseDrawAttributeTooltips()
{
	barState.drawAttributeBar.thicknessAnnotationHover = false;
	barState.drawAttributeBar.thicknessAnnotationPinned = false;
	barState.drawAttributeBar.thicknessAnnotationClosePress = false;
	barState.drawAttributeBar.thicknessOverflowHover = false;
	barState.drawAttributeBar.thicknessOverflowPinned = false;
	barState.drawAttributeBar.thicknessOverflowClosePress = false;
}

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
	unsigned long long barDeviceGeneration = 0;
	unsigned long long barDeviceResourceFailureGeneration = 0;
	bool barEndDrawFailureLogged = false;
	auto CreateBarDeviceResources = [&](const Ui3RenderDeviceEpoch& epoch) -> HRESULT
	{
		if (!epoch.d2dDevice) return E_POINTER;

		ComPtr<ID2D1DeviceContext> nextDeviceContext;
		ComPtr<ID2D1Bitmap1> nextBackgroundBitmap;
		ComPtr<ID2D1GdiInteropRenderTarget> nextGdiInterop;
		HRESULT hr = epoch.d2dDevice->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &nextDeviceContext);
		if (FAILED(hr)) return hr;

		D2D1_BITMAP_PROPERTIES1 bitmapProperties =
			D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
			);

		D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(barWindow.w), static_cast<UINT32>(barWindow.h));

		hr = nextDeviceContext->CreateBitmap(
			size,
			nullptr,
			0,
			&bitmapProperties,
			&nextBackgroundBitmap
		);
		if (FAILED(hr)) return hr;

		hr = nextDeviceContext.As(&nextGdiInterop);
		if (FAILED(hr)) return hr;

		nextDeviceContext->SetTarget(nextBackgroundBitmap.Get());
		nextDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

		// 新 epoch 完整就绪后再替换旧资源，避免 Hardware 准备失败时出现空白。
		barDeviceContext = move(nextDeviceContext);
		barBackgroundBitmap = move(nextBackgroundBitmap);
		barGdiInterop = move(nextGdiInterop);
		barDeviceGeneration = epoch.generation;
		barDeviceResourceFailureGeneration = 0;
		spec.DiscardDeviceResources();
		return S_OK;
	};
	{
		auto renderPass = AcquireUi3RenderPass(Ui3RenderPriority::Interactive);
		Ui3RenderDeviceEpoch epoch = GetUi3RenderDeviceEpoch();
		HRESULT hr = CreateBarDeviceResources(epoch);
		if (FAILED(hr))
		{
			if (IDTLogger) IDTLogger->error(
				"[BarUISetClass::Rendering] 创建 UI3 Bar 设备资源失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
			return;
		}
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
	const BarUiCurveSpecClass buttonPressCurve{
		BarUiCurveEnum::EaseOutCubic, BarUiCurveEnum::EaseOutCubic, 0.0, false };
	const BarUiCurveSpecClass buttonReleaseCurve{
		BarUiCurveEnum::EaseOutBack, BarUiCurveEnum::EaseOutBack, 0.0, false };
	optional<double> mainBarLayoutWidth;
	// 粗细预览使用独立动画值；切换画笔类型时曲线与数字共用同一进度。
	BarUiValueClass drawAttributePenThickness(max(0.0f, GetPenWidth()));
	bool drawAttributePenThicknessInitialized =
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
	BarUiValueClass drawAttributePenPreviewMorph(
		stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1 ? 1.0 : 0.0);
	bool drawAttributePenPreviewMorphInitialized =
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
	BarUiValueClass drawAttributeAnnotationPopupProgress(0.0);
	BarUiValueClass drawAttributeOverflowPopupProgress(0.0);
	D2D1_SIZE_F annotationLabelTextSize =
		spec.MeasureText(L"标注线", 13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F annotationPopupTitleSize = spec.MeasureText(
		L"启用标注线（暂不可用）",
		BarThicknessTooltipTitleFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
	D2D1_SIZE_F annotationPopupBodySize = spec.MeasureText(
		L"锁定绘制方向仅为水平、竖直或斜45°",
		BarThicknessTooltipBodyFontSize, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F overflowPopupTitleSize = spec.MeasureText(
		L"墨迹粗细超出预览范围",
		BarThicknessTooltipTitleFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
	D2D1_SIZE_F overflowPopupBodySize = spec.MeasureText(
		L"预览中的粗细可能与绘制粗细不一致。",
		BarThicknessTooltipBodyFontSize, DWRITE_FONT_WEIGHT_NORMAL);
	double annotationBadgeWidth = ceil(annotationLabelTextSize.width)
		+ 6.0 * 2.0 + 4.0 + BarThicknessTooltipIconSize;
	double annotationPopupWidth = ceil(max(
		annotationPopupTitleSize.width, annotationPopupBodySize.width))
		+ BarThicknessTooltipPadding * 2.0 + BarThicknessTooltipCloseReserve;
	double annotationPopupHeight = ceil(annotationPopupTitleSize.height
		+ BarThicknessTooltipLineGap + annotationPopupBodySize.height)
		+ BarThicknessTooltipPadding * 2.0;
	double overflowPopupWidth = ceil(max(
		overflowPopupTitleSize.width, overflowPopupBodySize.width))
		+ BarThicknessTooltipPadding * 2.0 + BarThicknessTooltipCloseReserve;
	double overflowPopupHeight = ceil(overflowPopupTitleSize.height
		+ BarThicknessTooltipLineGap + overflowPopupBodySize.height)
		+ BarThicknessTooltipPadding * 2.0;
	// 笔型按钮沿用主栏的独立按压缩放，不改变布局值与命中区域。
	BarUiValueClass drawAttributeBrushPressScale(1.0);
	BarUiValueClass drawAttributeHighlightPressScale(1.0);
	BarUiValueClass drawAttributeThicknessFinePressScale(1.0);
	BarUiValueClass drawAttributeThicknessMediumPressScale(1.0);
	BarUiValueClass drawAttributeThicknessCoarsePressScale(1.0);
	BarUiValueClass drawAttributeThicknessAdjustPressScale(1.0);
	BarUiValueClass drawAttributeAnnotationClosePressScale(1.0);
	BarUiValueClass drawAttributeOverflowClosePressScale(1.0);
	// 固定态结束后仍保留关闭图标，直到浮窗收起动画真正到达终点。
	bool drawAttributeAnnotationCloseVisible = false;
	bool drawAttributeOverflowCloseVisible = false;
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
		double currentAnimationSpeedRate = static_cast<double>(BarUiAnimationSpeedRate);

		// 主按钮
		{
			double operationDur = BarUiDefaultOperationDur;
			auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
			auto mainButtonInk = svgMap[BarUISetSvgEnum::logoInk];
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
				mainButtonInk->w.SetTar(mainButtonLogoBaseW, operationDur,
					mainButtonLogoBaseW * mainButtonScale, true, mainButtonPulseCurve);
				mainButtonInk->h.SetTar(mainButtonLogoBaseH, operationDur,
					mainButtonLogoBaseH * mainButtonScale, true, mainButtonPulseCurve);
			}
			else
			{
				mainButton->w.SetTar(mainButtonBaseSize, operationDur);
				mainButton->h.SetTar(mainButtonBaseSize, operationDur);
				mainButtonLogo->w.SetTar(mainButtonLogoBaseW, operationDur);
				mainButtonLogo->h.SetTar(mainButtonLogoBaseH, operationDur);
				mainButtonInk->w.SetTar(mainButtonLogoBaseW, operationDur);
				mainButtonInk->h.SetTar(mainButtonLogoBaseH, operationDur);
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
			superellipseMap[BarUISetSuperellipseEnum::MainButton]->fill.value().SetTar(
				GetThemeColor(BarThemeColorEnum::Surface), operationDur);
			superellipseMap[BarUISetSuperellipseEnum::MainButton]->frame.value().SetTar(
				GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);

			// 主按钮底图随深浅色切换，着色层跟随当前画笔颜色。
			{
				static optional<bool> lastMainLogoDarkStyle;
				bool currentMainLogoDarkStyle = barStyle.darkStyle;
				if (!lastMainLogoDarkStyle.has_value() || lastMainLogoDarkStyle.value() != currentMainLogoDarkStyle)
				{
					svgMap[BarUISetSvgEnum::logo1]->SetTarFromResource(L"UI", currentMainLogoDarkStyle ? L"logo1" : L"logo2");
					lastMainLogoDarkStyle = currentMainLogoDarkStyle;
				}
				// 着色层和底图同尺寸，贴合修正交给 SVG 路径本身处理。
				bool showLogoInk = stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
				// 显隐和换色共用 UI3 动画时钟，关闭动画时由全局倍率立即完成。
				mainButtonInk->color1.value().SetTar(GetPenColor(), operationDur);
				mainButtonInk->pct.SetTar(showLogoInk ? 1.0 : 0.0, operationDur);
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
					// 独立悬停已经提交自己的显现/淡出时长，不能再被属性栏批次时长覆盖。
					if (!pct.animateWhenDisabled && !pct.IsSame() && pct.progress == 0.0)
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
			{
				double penThickness = max(0.0f, GetPenWidth());
				double penPreviewMorph =
					PenModeUsesCurvedThicknessPreview(stateMode.Pen.ModeSelect)
					? 0.0
					: (stateMode.Pen.ModeSelect
						== PenModeSelectEnum::IdtPenHighlighter1 ? 1.0 : 0.0);
				if (!drawAttributePenThicknessInitialized)
				{
					// 首次进入绘制模式时先同步真实粗细，避免稍后展开属性栏仍显示 0 → 默认值。
					drawAttributePenThickness.SetDirect(penThickness);
					drawAttributePenThicknessInitialized = true;
				}
				else drawAttributePenThickness.SetTar(penThickness, operationDur);
				if (!drawAttributePenPreviewMorphInitialized)
				{
					drawAttributePenPreviewMorph.SetDirect(penPreviewMorph);
					drawAttributePenPreviewMorphInitialized = true;
				}
				else drawAttributePenPreviewMorph.SetTar(
					penPreviewMorph, operationDur);
			}
			else
			{
				drawAttributePenThicknessInitialized = false;
				drawAttributePenPreviewMorphInitialized = false;
			}
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
							if (!temp->IsVisible()) continue;
							if (yO <= 5.0) yO += 37.5, width += 37.5;
							else if (xO + 37.5 >= width) xO += 37.5, yO = 5.0;
							else xO += 37.5;
						}
						else if (temp->size == BarButtomSizeEnum::twoOne)
						{
							if (yO > 5.0 && xO + 75.0 > width) xO = width, yO = 5.0;
							if (!temp->IsVisible()) continue;
							if (yO <= 5.0) yO += 37.5, width += 75.0;
							else xO += 75.0, yO = 5.0;
						}
						else if (temp->size == BarButtomSizeEnum::twoTwo)
						{
							if (yO > 5.0) yO = 5.0, xO = width;
							if (temp->IsVisible()) xO += 75.0, width += 75.0;
						}
						else if (temp->size == BarButtomSizeEnum::oneTwo)
						{
							if (yO > 5.0) xO = width;
							if (temp->IsVisible()) xO += 15.0, yO = 5.0, width += 15.0;
						}
					}
					return width;
				};
			double layoutTotalWidth = CalculateButtonLayoutWidth();
			bool mainBarLayoutChange = mainBarLayoutWidth.has_value()
				&& abs(layoutTotalWidth - mainBarLayoutWidth.value()) > 0.000001;
			bool mainBarLayoutExpands = mainBarLayoutChange
				&& layoutTotalWidth > mainBarLayoutWidth.value();
			// 布局变化会取代仍在运行的换边关键帧；即使某个控件目标没变，也必须从当前值重建。
			bool interruptingMainBarSideSwitch = mainBarLayoutChange && mainBarTimeline.IsActive()
				&& (mainBar->x.hasMiddleV || mainBar->w.hasMiddleV);
			// 新操作创建完整批次；批次进入后半程后，新布局不再压缩到旧截止时间。
			bool lateMainBarLayoutChange = !barState.fold && mainBarTimeline.IsActive()
				&& mainBarLayoutChange && !mainBarTimeline.CanJoin();
			// 超过加入阈值后会创建新批次，此时旧换边中点已经失效，不能在新批次中再次收窄。
			bool continueMainBarSideSwitchKeyframe = interruptingMainBarSideSwitch
				&& !lateMainBarLayoutChange;
			bool restartMainBarTimeline = mainBarFoldChange || mainBarSideSwitch
				|| lateMainBarLayoutChange
				|| (!barState.fold && !mainBarTimeline.IsActive() && mainBarLayoutChange);
			if (restartMainBarTimeline)
			{
				if (mainBarSideSwitch) mainBarBatchCurve = BarUiCurveEnum::EaseInOutCubic;
				else if (mainBarFoldChange)
					mainBarBatchCurve = barState.fold
					? BarUiCurveEnum::EaseInBack : BarUiCurveEnum::EaseOutBack;
				else mainBarBatchCurve = mainBarLayoutExpands
					// 展开保留回弹活力；收起立即响应并在末端平稳减速到零。
					? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseOutCubic;
				mainBarTimeline.Restart(operationDur);
			}
			else if (mainBarTimeline.IsActive() && mainBarLayoutChange)
			{
				// 换边中途加入布局时用完整平滑曲线覆盖剩余时间，避免压缩重播 Back 造成突发加速。
				mainBarBatchCurve = continueMainBarSideSwitchKeyframe
					? BarUiCurveEnum::EaseInOutCubic
					: (mainBarLayoutExpands ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseOutCubic);
			}
			mainBarLayoutWidth = layoutTotalWidth;
			if (mainBarTimeline.IsActive()) operationDur = mainBarTimeline.GetRemainingDuration();
			double mainBarPhase = mainBarTimeline.IsActive() ? mainBarTimeline.GetProgress() : 0.0;
			bool continueMainBarPhase = mainBarTimeline.IsActive() && mainBarPhase > 0.0
				&& !continueMainBarSideSwitchKeyframe;
			syncValueCurveFromBatch = mainBarTimeline.IsActive();
			BarUiCurveEnum syncedMainBarCurve = mainBarTimeline.IsActive()
				? mainBarBatchCurve : BarUiCurveEnum::EaseInOutCubic;
			BarUiCurveEnum syncedMainBarPctCurve = mainBarLayoutChange
				? (mainBarLayoutExpands ? BarUiCurveEnum::EaseOutSine : BarUiCurveEnum::EaseOutCubic)
				: (mainBarTimeline.IsActive() && mainBarBatchCurve == BarUiCurveEnum::EaseInBack
					? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine);
			syncedValueCurve = { syncedMainBarCurve, syncedMainBarCurve,
				mainBarPhase, continueMainBarPhase };
			syncedPctCurve = { syncedMainBarPctCurve, syncedMainBarPctCurve,
				mainBarPhase, continueMainBarPhase };
			const BarUiCurveSpecClass continuedKeyframeValueCurve{
				BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack,
				mainBarPhase, true };
			const BarUiCurveSpecClass continuedKeyframePctCurve{
				BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine,
				mainBarPhase, true };
			auto SetButtonPositionTar = [&](BarUiValueClass& value, double target, double middle, bool mirrorX = false)
				{
					// 左侧展开仍按正序布局，只将最终横坐标按主栏宽度镜像。
					if (mirrorX && !barState.widgetPosition.mainBar) target = layoutTotalWidth - target;
					if (mainBarSideSwitch)
					{
						value.SetTar(target, operationDur, middle, true, keyframeValueCurve);
					}
					else if (continueMainBarSideSwitchKeyframe)
					{
						// 父栏和按钮必须继续共享换边中点，否则继承坐标会叠加出先后错位。
						value.SetTar(target, operationDur, middle, true, continuedKeyframeValueCurve);
					}
					else value.SetTar(target, operationDur, nullopt,
						mainBarFoldChange, syncedValueCurve);
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
								COLORREF iconColor = temp->state->state == BarWidgetState::Selected
									? GetThemeColor(BarThemeColorEnum::Accent)
									: GetThemeColor(BarThemeColorEnum::TextPrimary);
								// 第一次计算或不可见时直接同步，避免 SVG 显示后才从黑色过渡。
								if (forNum == 1 || barState.fold || !temp->IsVisible())
									temp->icon.color1.value().SetDirect(iconColor);
								else temp->icon.color1.value().SetTar(iconColor);
							}
							if (temp->preset.load() != BarButtomPresetEnum::Divider)
							{
								COLORREF buttonLightColor = temp->state->state == BarWidgetState::Selected
									? GetThemeColor(BarThemeColorEnum::Accent)
									: GetThemeColor(BarThemeColorEnum::TextPrimary);
								if (!temp->buttom.frame.has_value())
									temp->buttom.frame = BarUiColorClass(buttonLightColor);
								if (!temp->buttom.framePct.has_value())
									temp->buttom.framePct = BarUiPctClass(0.0);
								if (!temp->buttom.frameLightPct.has_value())
									temp->buttom.frameLightPct = BarUiPctClass(0.0);
								if (!temp->buttom.ft.has_value())
									temp->buttom.ft = BarUiValueClass(1.0);
								temp->buttom.frameRendering = BarUiFrameRenderingEnum::PointLight;
								temp->buttom.framePrimaryLightEnabled = false;
								temp->buttom.frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
								if (forNum == 1 || barState.fold || temp->hide)
									temp->buttom.frame.value().SetDirect(buttonLightColor);
								else temp->buttom.frame.value().SetTar(buttonLightColor);
								// 主栏仅让选中按钮响应第三光源，未选中按钮保持无光影。
								bool buttonLightVisible = !barState.fold && !temp->hide
									&& temp->buttom.enable.tar
									&& temp->state->state == BarWidgetState::Selected;
								double buttonLightOpacity = buttonLightVisible
									? (temp->state->emph == BarWidgetEmphasize::Pressed
										? BarButtonPressedLightOpacity : 1.0) : 0.0;
								temp->buttom.frameLightPct.value().SetTar(buttonLightOpacity, operationDur);
							}

							if (temp->size == BarButtomSizeEnum::oneOne)
							{
								// 特殊设定：是否是颜色选择器
								bool isColorSelector = (temp->name.enable.tar && temp->name.content.GetTar().substr(0, 7) == L"__color");

								if (temp->buttom.enable.tar)
								{
									if (barState.fold || !temp->IsVisible())
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
											else if (temp->hoverStage == BarButtomHoverStageEnum::None)
												temp->buttom.pct.SetTar(0.0, operationDur);
										}
									}
								temp->buttom.w.SetTar(30.0, operationDur);
								temp->buttom.h.SetTar(30.0, operationDur);

									if (!isColorSelector)
									{
										if (temp->state->state == BarWidgetState::Selected)
											temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
										else temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
									}
								}
								if (temp->icon.enable.tar)
								{
									if (isColorSelector) temp->icon.SetWH(nullopt, 10.0); // 颜色选择器中的图标即为标识选中该颜色，所以需要较小尺寸
									else temp->icon.SetWH(nullopt, 20.0);

									temp->icon.x.SetTar(0.0);
									temp->icon.y.SetTar(0.0);
									if (barState.fold || !temp->IsVisible())
									{
										temp->icon.pct.SetTar(0.0, operationDur);
									}
									else
									{
										temp->icon.pct.SetTar(1.0, operationDur);
										if (temp->state->state == BarWidgetState::Selected)
											temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
										else temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
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

								if (!temp->IsVisible())
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
									if (barState.fold || !temp->IsVisible())
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
									else if (temp->hoverStage == BarButtomHoverStageEnum::None)
										temp->buttom.pct.SetTar(0.0, operationDur);
									}
								temp->buttom.w.SetTar(70.0, operationDur);
								temp->buttom.h.SetTar(30.0, operationDur);

								if (temp->state->state == BarWidgetState::Selected)
									temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
								else temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 18.0);

									temp->icon.x.SetTar(-21.0); // 靠左对齐（上下两侧均保持 6px 的空隙，而左侧是 5px）
									temp->icon.y.SetTar(0.0);
									if (barState.fold || !temp->IsVisible()) temp->icon.pct.SetTar(0.0, operationDur);
									else
									{
										temp->icon.pct.SetTar(1.0, operationDur);
										if (temp->state->state == BarWidgetState::Selected)
											temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
										else temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
									}
								}
								if (temp->name.enable.tar)
								{
									temp->name.x.SetTar(11.5); // 右对齐
									temp->name.y.SetTar(0.0);
									temp->name.w.SetTar(37); // 70px 宽度中除去左侧 icon 占用的 18px + 5px * 2 的空隙,考虑自身右侧还有 5px 的间隙
									temp->name.h.SetTar(30.0);
									if (barState.fold || !temp->IsVisible()) temp->name.pct.SetTar(0.0, operationDur);
									else temp->name.pct.SetTar(1.0, operationDur);

									if (temp->state->state == BarWidgetState::Selected)
										temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::Accent));
									else temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
									temp->name.size.SetTar(12.0);
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (!temp->IsVisible())
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
									if (barState.fold || !temp->IsVisible())
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
									else if (temp->hoverStage == BarButtomHoverStageEnum::None)
										temp->buttom.pct.SetTar(0.0, operationDur);
									}
								temp->buttom.w.SetTar(70.0, operationDur);
								temp->buttom.h.SetTar(70.0, operationDur);

								if (temp->state->state == BarWidgetState::Selected)
									temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
								else temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 28.0);
									temp->icon.x.SetTar(0.0);
									temp->icon.y.SetTar(-10.0);
									if (barState.fold || !temp->IsVisible())
									{
										temp->icon.pct.SetTar(0.0, operationDur);
									}
									else
									{
										temp->icon.pct.SetTar(1.0, operationDur);
										if (temp->state->state == BarWidgetState::Selected)
											temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
										else temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
									}
								}
								if (temp->name.enable.tar)
								{
									temp->name.x.SetTar(0.0);
									temp->name.y.SetTar(20.0);
									temp->name.w.SetTar(70.0);
									temp->name.h.SetTar(25.0);
									if (barState.fold || !temp->IsVisible()) temp->name.pct.SetTar(0.0, operationDur);
									else temp->name.pct.SetTar(1.0, operationDur);

									if (temp->state->state == BarWidgetState::Selected)
										temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::Accent));
									else temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));

									temp->name.size.SetTar(13.0);
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (!temp->IsVisible())
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
									if (barState.fold || !temp->IsVisible())
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
									else if (temp->hoverStage == BarButtomHoverStageEnum::None)
										temp->buttom.pct.SetTar(0.0, operationDur);
									}
								temp->buttom.w.SetTar(10.0, operationDur);
								temp->buttom.h.SetTar(70.0, operationDur);

								// 分割线没有选中状态，隐藏时也预存灰色，避免悬停显现前段混入青色。
								temp->buttom.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
								}
								if (temp->icon.enable.tar)
								{
									temp->icon.SetWH(nullopt, 60.0);
									if (barState.fold || !temp->IsVisible())
									{
										temp->icon.pct.SetTar(0.0, operationDur);
									}
									else
									{
										temp->icon.pct.SetTar(0.18, operationDur);
										temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
									}
								}

								// 记录目标绘制位置
								temp->lastDrawX = temp->buttom.x.tar;
								temp->lastDrawY = temp->buttom.y.tar;

								if (!temp->IsVisible())
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

							// 按压倍率独立于布局批次，松手或拖出时从当前值回弹到标准大小。
							if (temp->state->emph == BarWidgetEmphasize::Pressed)
								temp->pressScale.SetTar(BarButtonPressScale, BarUiDefaultOperationDur,
									nullopt, false, buttonPressCurve);
							else temp->pressScale.SetTar(1.0, BarUiDefaultOperationDur,
								nullopt, false, buttonReleaseCurve);

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

							if (mainBarSideSwitch || continueMainBarSideSwitchKeyframe)
							{
								// 换边中点将整个按钮组合隐藏，再从主按钮下方展开到新位置。
								const BarUiCurveSpecClass& pctCurve = mainBarSideSwitch
									? keyframePctCurve : continuedKeyframePctCurve;
								temp->buttom.pct.SetTar(temp->buttom.pct.tar, operationDur, 0.0, true, pctCurve);
								if (temp->buttom.frameLightPct.has_value())
									temp->buttom.frameLightPct.value().SetTar(
										temp->buttom.frameLightPct.value().tar, operationDur, 0.0, true, pctCurve);
								temp->icon.pct.SetTar(temp->icon.pct.tar, operationDur, 0.0, true, pctCurve);
								temp->name.pct.SetTar(temp->name.pct.tar, operationDur, 0.0, true, pctCurve);
							}
						}
					}, viewVariant);

				auto FindVisibleAnchor = [&](BarButtomClass* hidden, BarButtomClass* preferred)
					{
						const int buttonCount = barButtomSet.tot.load();
						int hiddenIndex = -1;
						for (int index = 0; index < buttonCount; index++)
						{
							BarButtomClass* candidate = barButtomSet.buttomlist.Get(index);
							if (candidate == hidden) hiddenIndex = index;
							if (candidate && candidate == preferred && candidate->IsVisible()) return candidate;
						}
						if (hiddenIndex < 0) return static_cast<BarButtomClass*>(nullptr);

						// 首选锚点不可见时，按布局距离寻找最近的有效按钮。
						for (int distance = 1; distance < buttonCount; distance++)
						{
							const int previousIndex = hiddenIndex - distance;
							if (previousIndex >= 0)
							{
								BarButtomClass* candidate = barButtomSet.buttomlist.Get(previousIndex);
								if (candidate && candidate->IsVisible()) return candidate;
							}

							const int nextIndex = hiddenIndex + distance;
							if (nextIndex < buttonCount)
							{
								BarButtomClass* candidate = barButtomSet.buttomlist.Get(nextIndex);
								if (candidate && candidate->IsVisible()) return candidate;
							}
						}
						return static_cast<BarButtomClass*>(nullptr);
					};
				auto AnchorHiddenButton = [&](BarButtomPresetEnum hiddenPreset, BarButtomPresetEnum anchorPreset)
					{
						BarButtomClass* hidden = barButtomSet.preset[static_cast<int>(hiddenPreset)];
						if (barState.fold || !hidden || hidden->IsVisible()) return;

						BarButtomClass* anchor = FindVisibleAnchor(
							hidden, barButtomSet.preset[static_cast<int>(anchorPreset)]);
						if (!anchor) return;

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
					if (mainBarSideSwitch)
						mainBar->w.SetTar(totalWidth, operationDur, 80.0, true, keyframeValueCurve);
					else if (continueMainBarSideSwitchKeyframe)
						// 布局目标变化不能丢掉换边中点，继续在原批次 0.5 时刻收窄到主按钮宽度。
						mainBar->w.SetTar(totalWidth, operationDur, 80.0, true, continuedKeyframeValueCurve);
					else mainBar->w.SetTar(totalWidth, operationDur, nullopt,
						mainBarFoldChange, syncedValueCurve);

					double targetX = 0.0;
					if (barState.widgetPosition.mainBar)
						targetX = superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + mainBar->w.tar / 2.0 + 10.0;
					else
						targetX = -(superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + mainBar->w.tar / 2.0 + 10.0);

					if (mainBarSideSwitch)
						mainBar->x.SetTar(targetX, operationDur, 0.0, true, keyframeValueCurve);
					else if (continueMainBarSideSwitchKeyframe)
						mainBar->x.SetTar(targetX, operationDur, 0.0, true, continuedKeyframeValueCurve);
					else mainBar->x.SetTar(targetX, operationDur, nullopt,
						mainBarFoldChange, syncedValueCurve);

					shapeMap[BarUISetShapeEnum::MainBar]->pct.SetTar(
						0.8, operationDur, nullopt, false, syncedPctCurve);
					shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().SetTar(
						0.18, operationDur, nullopt, false, syncedPctCurve);
				}
				if (mainBarSideSwitch || continueMainBarSideSwitchKeyframe)
				{
					// 主栏填充和边框在换边关键帧同步变为全透明。
					const BarUiCurveSpecClass& pctCurve = mainBarSideSwitch
						? keyframePctCurve : continuedKeyframePctCurve;
					mainBar->pct.SetTar(mainBar->pct.tar, operationDur, 0.0, true, pctCurve);
					mainBar->framePct.value().SetTar(
						mainBar->framePct.value().tar, operationDur, 0.0, true, pctCurve);
				}
				shapeMap[BarUISetShapeEnum::MainBar]->fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Surface));
				shapeMap[BarUISetShapeEnum::MainBar]->frame.value().SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));

				// 绘制属性
				{
					bool drawAttributeBatchChange = drawAttributeVisibilityChange || drawAttributeSideSwitch;
					operationDur = BarUiDefaultOperationDur;
					double drawAttributePhase = 0.0;
					bool continueDrawAttributePhase = false;
					if (drawAttributeBatchChange)
					{
						// 只在主栏批次前 50% 内加入；进入后半程则使用完整时长创建独立新批次。
						if (mainBarTimeline.CanJoin())
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
					auto CompactDrawAttributeX = [&](double expandedX) { return expandedX * BarDrawAttributeCompactScale; };
					auto CompactDrawAttributeY = [&](double expandedY)
						{
							return expandedY * BarDrawAttributeCompactScale;
						};
					auto CompactDrawAttributeSize = [&](double expandedSize)
						{
							return expandedSize * BarDrawAttributeCompactScale;
						};
					double drawAttributeLayoutScale =
						barState.drawAttribute ? 1.0 : BarDrawAttributeCompactScale;
					auto drawAttributeBar =
						shapeMap[BarUISetShapeEnum::DrawAttributeBar];
					if (!barState.drawAttribute)
					{
						// 收起面板保持与展开面板相同宽高比，并居中藏在绘制按钮下方。
						drawAttributeBar->x.SetTar(0.0);
						drawAttributeBar->y.SetTar(0.0);
						drawAttributeBar->w.SetTar(BarDrawAttributeCompactWidth);
						drawAttributeBar->h.SetTar(BarDrawAttributeCompactHeight);

						drawAttributeBar->pct.SetTar(0.0);
						drawAttributeBar->framePct.value().SetTar(0.0);
					}
					else
					{
						drawAttributeBar->w.SetTar(BarDrawAttributeExpandedWidth);
						drawAttributeBar->h.SetTar(BarDrawAttributeExpandedHeight);

						drawAttributeBar->x.SetTar(0);
						if (barState.widgetPosition.primaryBar)
							drawAttributeBar->y.SetTar((shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + drawAttributeBar->GetH() / 2.0 + 10.0));
						else
							drawAttributeBar->y.SetTar(-(shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + drawAttributeBar->GetH() / 2.0 + 10.0));

						drawAttributeBar->pct.SetTar(
							BarDrawAttributeSurfaceOpacity);
						drawAttributeBar->framePct.value().SetTar(0.18);
					}
					drawAttributeBar->rw.value().SetTar(8.0 * drawAttributeLayoutScale);
					drawAttributeBar->rh.value().SetTar(8.0 * drawAttributeLayoutScale);
					drawAttributeBar->ft.value().SetTar(drawAttributeLayoutScale);
					drawAttributeBar->fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Surface));
					drawAttributeBar->frame.value().SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));

					// Color 区域
					{
						// 上下布局直接提交最终坐标，避免同一帧多次 SetTar 重启动画。
						double colorTopY = barState.widgetPosition.primaryBar ? 5.0 : 115.0;
						double colorBottomY = barState.widgetPosition.primaryBar ? 40.0 : 150.0;
						// Color 1
						{
							if (!barState.drawAttribute)
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.SetTar(CompactDrawAttributeX(5.0));
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(
									CompactDrawAttributeY(colorTopY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.SetTar(5.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(colorTopY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->fill.value().tar))
							{
								// 说明当前选中的是当前的颜色
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(
									CompactDrawAttributeY(colorBottomY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.SetTar(5.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(colorBottomY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(
									CompactDrawAttributeY(colorTopY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.SetTar(40.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(colorTopY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(
									CompactDrawAttributeY(colorBottomY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.SetTar(40.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(colorBottomY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(
									CompactDrawAttributeY(colorTopY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.SetTar(75.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(colorTopY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(
									CompactDrawAttributeY(colorBottomY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.SetTar(75.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(colorBottomY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(
									CompactDrawAttributeY(colorTopY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.SetTar(110.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(colorTopY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(
									CompactDrawAttributeY(colorBottomY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.SetTar(110.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(colorBottomY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(
									CompactDrawAttributeY(colorTopY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.SetTar(145.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(colorTopY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(
									CompactDrawAttributeY(colorBottomY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.SetTar(145.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(colorBottomY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().SetTar(1.0);
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
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(
									CompactDrawAttributeY(colorTopY));

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(0.0);
							}
							else
							{
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.SetTar(180.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(colorTopY);

								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(1.0);
							}

							if (barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->fill.value().tar))
							{
								svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(1.0);
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().SetTar(1.0);
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
						struct PenTypeButtonLayout
						{
							BarUISetShapeEnum shape;
							BarUISetSvgEnum svg;
							BarUISetWordEnum word;
							double y;
							bool enabled;
							bool selected;
							bool pressed;
							IdtAtomic<BarButtomHoverStageEnum>* hoverStage;
							BarUiValueClass* pressScale;
						};
						const PenTypeButtonLayout penTypeButtons[] =
						{
							{ BarUISetShapeEnum::DrawAttributeBar_Brush2,
								BarUISetSvgEnum::DrawAttributeBar_Brush2,
								BarUISetWordEnum::DrawAttributeBar_Brush2,
								5.0, false, false, false, nullptr, nullptr },
							{ BarUISetShapeEnum::DrawAttributeBar_Laser,
								BarUISetSvgEnum::DrawAttributeBar_Laser,
								BarUISetWordEnum::DrawAttributeBar_Laser,
								40.0, false, false, false, nullptr, nullptr },
							{ BarUISetShapeEnum::DrawAttributeBar_Highlight1,
								BarUISetSvgEnum::DrawAttributeBar_Highlight1,
								BarUISetWordEnum::DrawAttributeBar_Highlight1,
								75.0, true,
								stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1,
								barState.drawAttributeBar.highlight1Press,
								&drawAttributeHighlightHoverStage, &drawAttributeHighlightPressScale },
							{ BarUISetShapeEnum::DrawAttributeBar_Brush1,
								BarUISetSvgEnum::DrawAttributeBar_Brush1,
								BarUISetWordEnum::DrawAttributeBar_Brush1,
								110.0, true,
								stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1,
								barState.drawAttributeBar.brush1Press,
								&drawAttributeBrushHoverStage, &drawAttributeBrushPressScale },
							{ BarUISetShapeEnum::DrawAttributeBar_SoftPen,
								BarUISetSvgEnum::DrawAttributeBar_SoftPen,
								BarUISetWordEnum::DrawAttributeBar_SoftPen,
								145.0, false, false, false, nullptr, nullptr },
						};
						for (const auto& button : penTypeButtons)
						{
							auto shape = shapeMap[button.shape];
							auto svg = svgMap[button.svg];
							auto word = wordMap[button.word];
							double layoutScale = drawAttributeLayoutScale;

							shape->x.SetTar(250.0 * layoutScale);
							shape->y.SetTar(button.y * layoutScale);
							shape->w.SetTar(115.0 * layoutScale);
							shape->h.SetTar(30.0 * layoutScale);
							shape->rw.value().SetTar(4.0 * layoutScale);
							shape->rh.value().SetTar(4.0 * layoutScale);
							shape->ft.value().SetTar(layoutScale);
							svg->x.SetTar(6.0 * layoutScale);
							svg->y.SetTar(0.0);
							svg->SetWH(18.0 * layoutScale, 18.0 * layoutScale);
							word->x.SetTar(-5.0 * layoutScale);
							word->y.SetTar(0.0);
							word->w.SetTar(80.0 * layoutScale);
							word->h.SetTar(30.0 * layoutScale);
							word->size.SetTar(12.0 * layoutScale);
							if (button.enabled)
							{
								if (!shape->frame.has_value()) shape->frame = BarUiColorClass(GetThemeColor(BarThemeColorEnum::TextPrimary));
								if (!shape->framePct.has_value()) shape->framePct = BarUiPctClass(0.0);
								if (!shape->frameLightPct.has_value()) shape->frameLightPct = BarUiPctClass(0.0);
								if (!shape->ft.has_value()) shape->ft = BarUiValueClass(1.0);
								shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
								shape->framePrimaryLightEnabled = false;
								shape->frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
							}

							if (!barState.drawAttribute)
							{
								shape->pct.SetTar(0.0);
								svg->pct.SetTar(0.0);
								word->pct.SetTar(0.0);
								if (shape->frameLightPct.has_value()) shape->frameLightPct->SetTar(0.0);
							}
							else
							{
								// 预留笔型只禁用交互与光影，图标和文字仍正常显示。
								svg->pct.SetTar(1.0);
								word->pct.SetTar(1.0);
								if (!button.enabled) shape->pct.SetTar(0.0);
								else if (button.pressed) shape->pct.SetTar(0.1);
								else if (button.selected) shape->pct.SetTar(0.2);
								else if (button.hoverStage
									&& *button.hoverStage == BarButtomHoverStageEnum::None)
									shape->pct.SetTar(0.0);
								if (shape->frameLightPct.has_value())
									shape->frameLightPct->SetTar(button.enabled && button.selected
										? (button.pressed ? BarButtonPressedLightOpacity : 1.0) : 0.0);
							}

							COLORREF contentColor = button.enabled
								? (button.selected
									? GetThemeColor(BarThemeColorEnum::Accent)
									: GetThemeColor(BarThemeColorEnum::TextPrimary))
								: RGB(200, 200, 200);
							word->color.SetTar(contentColor);
							SetDrawAttributeSvgColor(button.svg, contentColor);
							if (button.enabled) shape->frame.value().SetTar(contentColor);
							shape->fill.value().SetTar(button.selected
								? GetThemeColor(BarThemeColorEnum::Accent)
								: GetThemeColor(BarThemeColorEnum::PressedFill));

							if (button.pressScale)
							{
								button.pressScale->SetTar(
									button.pressed ? BarButtonPressScale : 1.0,
									BarUiDefaultOperationDur, nullopt, false,
									button.pressed ? buttonPressCurve : buttonReleaseCurve);
							}
						}
					}
					{ /**/ }
					// 粗细调节区域
					{
						double layoutScale = drawAttributeLayoutScale;
						double thicknessY = barState.widgetPosition.primaryBar ? 75.0 : 5.0;
						bool thicknessControlsOnTop =
							barState.widgetPosition.primaryBar;
						double thicknessControlOffsetY = thicknessControlsOnTop
							? BarDrawAttributeGap
							: BarDrawAttributeThicknessHeight
								- BarDrawAttributeGap
								- BarDrawAttributeThicknessControlHeight;
						auto thicknessRegion =
							shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
						thicknessRegion->x.SetTar(
							BarDrawAttributeGap * layoutScale);
						thicknessRegion->y.SetTar(thicknessY * layoutScale);
						thicknessRegion->w.SetTar(240.0 * layoutScale);
						thicknessRegion->h.SetTar(
							BarDrawAttributeThicknessHeight * layoutScale);
						thicknessRegion->rw.value().SetTar(4.0 * layoutScale);
						thicknessRegion->rh.value().SetTar(4.0 * layoutScale);
						thicknessRegion->ft.value().SetTar(layoutScale);
						thicknessRegion->pct.SetTar(barState.drawAttribute ? 1.0 : 0.0);
						thicknessRegion->framePct.value().SetTar(
							barState.drawAttribute ? 0.18 : 0.0);
						thicknessRegion->frameLightPct.value().SetTar(
							barState.drawAttribute ? 1.0 : 0.0);
						thicknessRegion->frame.value().SetTar(
							GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);

						auto thicknessDisplay =
							wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay];
						thicknessDisplay->x.SetTar(
							(BarDrawAttributeGap
								+ BarDrawAttributeThicknessContentInset)
							* layoutScale);
						thicknessDisplay->y.SetTar(
							(thicknessY + thicknessControlOffsetY) * layoutScale);
						thicknessDisplay->w.SetTar(90.0 * layoutScale);
						thicknessDisplay->h.SetTar(30.0 * layoutScale);
						thicknessDisplay->size.SetTar(13.0 * layoutScale);
						thicknessDisplay->pct.SetTar(barState.drawAttribute ? 1.0 : 0.0);
						thicknessDisplay->color.SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary));

						bool brushMode =
							stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1;
						int displayedThickness = static_cast<int>(lround(clamp(
							static_cast<double>(drawAttributePenThickness.val),
							0.0, 999.0)));
						auto ConfigureThicknessButton = [&](BarUISetShapeEnum shapeType,
							shared_ptr<BarUiWordClass> numberWord, double x, bool visible,
							bool selected, bool pressed,
							IdtAtomic<BarButtomHoverStageEnum>& hoverStage,
							BarUiValueClass& pressScale)
							{
								auto shape = shapeMap[shapeType];
								shape->x.SetTar(x * layoutScale);
								shape->y.SetTar(
									(thicknessY + thicknessControlOffsetY) * layoutScale);
								shape->w.SetTar(
									BarDrawAttributeThicknessControlHeight * layoutScale);
								shape->h.SetTar(
									BarDrawAttributeThicknessControlHeight * layoutScale);
								shape->rw.value().SetTar(4.0 * layoutScale);
								shape->rh.value().SetTar(4.0 * layoutScale);
								shape->fill.value().SetTar(selected
									? GetThemeColor(BarThemeColorEnum::Accent)
									: GetThemeColor(BarThemeColorEnum::PressedFill));
								shape->frame.value().SetTar(selected
									? GetThemeColor(BarThemeColorEnum::Accent)
									: GetThemeColor(BarThemeColorEnum::TextPrimary));

								if (!visible)
								{
									shape->pct.SetTar(0.0);
									shape->frameLightPct.value().SetTar(0.0);
									if (numberWord) numberWord->pct.SetTar(0.0);
								}
								else
								{
									if (pressed) shape->pct.SetTar(0.10);
									else if (selected) shape->pct.SetTar(0.20);
									else if (hoverStage == BarButtomHoverStageEnum::None)
										shape->pct.SetTar(0.0);
									shape->frameLightPct.value().SetTar(selected
										? (pressed ? BarButtonPressedLightOpacity : 1.0) : 0.0);
									if (numberWord) numberWord->pct.SetTar(1.0);
								}

								if (numberWord)
								{
									numberWord->x.SetTar(x * layoutScale);
									numberWord->y.SetTar(
										(thicknessY + thicknessControlOffsetY) * layoutScale);
									numberWord->w.SetTar(
										BarDrawAttributeThicknessControlHeight * layoutScale);
									numberWord->h.SetTar(
										BarDrawAttributeThicknessControlHeight * layoutScale);
									numberWord->size.SetTar(10.0 * layoutScale);
								}
								pressScale.SetTar(pressed ? BarButtonPressScale : 1.0,
									BarUiDefaultOperationDur, nullopt, false,
									pressed ? buttonPressCurve : buttonReleaseCurve);
							};

						const BarUISetShapeEnum presetShapes[] =
						{
							BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
							BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
							BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
						};
						const BarUISetWordEnum presetWords[] =
						{
							BarUISetWordEnum::DrawAttributeBar_ThicknessFineNumber,
							BarUISetWordEnum::DrawAttributeBar_ThicknessMediumNumber,
							BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber,
						};
						IdtAtomic<bool>* presetPresses[] =
						{
							&barState.drawAttributeBar.thicknessFinePress,
							&barState.drawAttributeBar.thicknessMediumPress,
							&barState.drawAttributeBar.thicknessCoarsePress,
						};
						IdtAtomic<BarButtomHoverStageEnum>* presetHoverStages[] =
						{
							&drawAttributeThicknessFineHoverStage,
							&drawAttributeThicknessMediumHoverStage,
							&drawAttributeThicknessCoarseHoverStage,
						};
						BarUiValueClass* presetPressScales[] =
						{
							&drawAttributeThicknessFinePressScale,
							&drawAttributeThicknessMediumPressScale,
							&drawAttributeThicknessCoarsePressScale,
						};
						for (size_t i = 0; i < 3; ++i)
						{
							int presetPx = GetBarBrushThicknessPresetPx(i, barStyle.dpiZoom);
							auto numberWord = wordMap[presetWords[i]];
							wstring numberText = to_wstring(presetPx);
							numberWord->content.SetTar(numberText);
							ConfigureThicknessButton(presetShapes[i], numberWord,
								100.0 + static_cast<double>(i) * 35.0,
								barState.drawAttribute && brushMode,
								displayedThickness == presetPx, *presetPresses[i],
								*presetHoverStages[i], *presetPressScales[i]);
						}
						bool adjustVisible = barState.drawAttribute
							&& (brushMode || stateMode.Pen.ModeSelect
								== PenModeSelectEnum::IdtPenHighlighter1);
						ConfigureThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust, nullptr,
							205.0, adjustVisible, false,
							barState.drawAttributeBar.thicknessAdjustPress,
							drawAttributeThicknessAdjustHoverStage,
							drawAttributeThicknessAdjustPressScale);
						auto thicknessAdjustSvg =
							svgMap[BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust];
						thicknessAdjustSvg->x.SetTar(0.0);
						thicknessAdjustSvg->y.SetTar(0.0);
						thicknessAdjustSvg->w.SetTar(18.0 * layoutScale);
						thicknessAdjustSvg->h.SetTar(18.0 * layoutScale);
						thicknessAdjustSvg->pct.SetTar(adjustVisible ? 1.0 : 0.0);
						auto& thicknessAdjustColor =
							thicknessAdjustSvg->color1.value();
						COLORREF thicknessAdjustTargetColor =
							GetThemeColor(BarThemeColorEnum::TextPrimary);
						if (forNum == 1 || !barState.drawAttribute)
							thicknessAdjustColor.SetDirect(thicknessAdjustTargetColor);
						else thicknessAdjustColor.SetTar(thicknessAdjustTargetColor);

						bool tooltipBaseVisible =
							barState.drawAttribute && !barState.fold;
						bool annotationSupported = tooltipBaseVisible
							&& PenModeSupportsAnnotationLine(
								stateMode.Pen.ModeSelect);
						double expandedPreviewCapacity =
							(BarDrawAttributeThicknessHeight
								- BarDrawAttributeThicknessControlHeight
								- BarDrawAttributeGap * 3.0)
							* max(0.0, static_cast<double>(barStyle.zoom));
						// 测试阶段始终开放超限徽标；关闭该常量即可恢复真实粗细门禁。
						bool previewOverflow = tooltipBaseVisible
							&& (BarThicknessOverflowPreviewAlwaysVisible
								|| static_cast<double>(GetPenWidth())
									> expandedPreviewCapacity + 0.001);
						barState.drawAttributeBar.thicknessPreviewOverflow =
							previewOverflow;

						if (!tooltipBaseVisible) CloseDrawAttributeTooltips();
						if (!annotationSupported)
						{
							barState.drawAttributeBar.thicknessAnnotationHover = false;
							barState.drawAttributeBar.thicknessAnnotationPinned = false;
							barState.drawAttributeBar.thicknessAnnotationClosePress = false;
						}
						if (!previewOverflow)
						{
							barState.drawAttributeBar.thicknessOverflowHover = false;
							barState.drawAttributeBar.thicknessOverflowPinned = false;
							barState.drawAttributeBar.thicknessOverflowClosePress = false;
						}

						auto SetTooltipProgress = [&](BarUiValueClass& progress,
							bool visible)
							{
								BarUiCurveEnum curve = visible
									? BarUiCurveEnum::EaseOutBack
									: BarUiCurveEnum::EaseInBack;
								BarUiCurveSpecClass curveSpec{
									curve, curve, 0.0, false };
								progress.SetTar(visible ? 1.0 : 0.0,
									operationDur, nullopt, false, curveSpec);
							};
						SetTooltipProgress(drawAttributeAnnotationPopupProgress,
							annotationSupported
							&& (barState.drawAttributeBar.thicknessAnnotationHover
								|| barState.drawAttributeBar.thicknessAnnotationPinned));
						SetTooltipProgress(drawAttributeOverflowPopupProgress,
							previewOverflow
							&& (barState.drawAttributeBar.thicknessOverflowHover
								|| barState.drawAttributeBar.thicknessOverflowPinned));

						auto ConfigureTooltipCloseButton =
							[&](BarUISetShapeEnum shapeType, bool pinned,
								bool pressed,
								IdtAtomic<BarButtomHoverStageEnum>& hoverStage,
								BarUiValueClass& pressScale)
							{
								auto closeButton = shapeMap[shapeType];
								closeButton->fill.value().SetTar(
									GetThemeColor(BarThemeColorEnum::PressedFill),
									operationDur);
								if (!pinned) closeButton->pct.SetTar(0.0);
								else if (pressed) closeButton->pct.SetTar(0.10);
								else if (hoverStage == BarButtomHoverStageEnum::None)
									closeButton->pct.SetTar(0.0);
								pressScale.SetTar(
									pressed ? BarButtonPressScale : 1.0,
									BarUiDefaultOperationDur, nullopt, false,
									pressed ? buttonPressCurve : buttonReleaseCurve);
							};
						ConfigureTooltipCloseButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
							barState.drawAttributeBar.thicknessAnnotationPinned,
							barState.drawAttributeBar.thicknessAnnotationClosePress,
							drawAttributeAnnotationCloseHoverStage,
							drawAttributeAnnotationClosePressScale);
						ConfigureTooltipCloseButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
							barState.drawAttributeBar.thicknessOverflowPinned,
							barState.drawAttributeBar.thicknessOverflowClosePress,
							drawAttributeOverflowCloseHoverStage,
							drawAttributeOverflowClosePressScale);

						const BarUISetShapeEnum tooltipSurfaces[] =
						{
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationBadge,
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge,
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
						};
						for (auto shapeType : tooltipSurfaces)
						{
							auto surface = shapeMap[shapeType];
							surface->fill.value().SetTar(
								GetThemeColor(BarThemeColorEnum::Surface),
								operationDur);
							surface->frame.value().SetTar(
								GetThemeColor(BarThemeColorEnum::SurfaceFrame),
								operationDur);
						}
						wordMap[
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel]
							->color.SetTar(RGB(200, 200, 200), operationDur);
						wordMap[
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText]
							->color.SetTar(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								operationDur);
						COLORREF popupBodyColor = MixBarUiColor(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							GetThemeColor(BarThemeColorEnum::Surface), 0.45);
						wordMap[
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody]
							->color.SetTar(popupBodyColor, operationDur);
						wordMap[
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText]
							->color.SetTar(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								operationDur);
						wordMap[
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody]
							->color.SetTar(popupBodyColor, operationDur);
						svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo]
							->color1.value().SetTar(
								RGB(200, 200, 200), operationDur);
						svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo]
							->color1.value().SetTar(
								RGB(255, 255, 255), operationDur);
						svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose]
							->color1.value().SetTar(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								operationDur);
						svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose]
							->color1.value().SetTar(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								operationDur);
					}

					// 颜色块在收起状态保留缩小后的相对排布，展开时同时恢复坐标和尺寸。
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
					{
						auto shape = shapeMap[static_cast<BarUISetShapeEnum>(i)];
						double size = barState.drawAttribute ? 30.0 : CompactDrawAttributeSize(30.0);
						shape->w.SetTar(size);
						shape->h.SetTar(size);
						shape->rw.value().SetTar(4.0 * drawAttributeLayoutScale);
						shape->rh.value().SetTar(4.0 * drawAttributeLayoutScale);
						shape->ft.value().SetTar(drawAttributeLayoutScale);
						// 填充先显现，灰边只随同一批次淡入到 18%。
						shape->framePct.value().SetTar(
							barState.drawAttribute ? BarColorSwatchFrameOpacity : 0.0);

						auto svg = svgMap[static_cast<BarUISetSvgEnum>(
							static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1)
							+ i - static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1))];
						double svgSize = barState.drawAttribute ? 15.0 : CompactDrawAttributeSize(15.0);
						svg->w.SetTar(svgSize);
						svg->h.SetTar(svgSize);
					}

					// 展开、收起时，属性栏及全部内部控件共用同一个完成时刻。
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust); i++)
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
						if (obj->frameLightPct.has_value()) SyncPctDuration(obj->frameLightPct.value());
					}
					for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust); i++)
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
						i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber); i++)
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
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust); i++)
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
							if (obj->frameLightPct.has_value())
								obj->frameLightPct.value().SetTar(obj->frameLightPct.value().tar, operationDur, nullopt, true, syncedPctCurve);
						}
						for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust); i++)
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
							i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber); i++)
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
						// 所有隐藏几何都约束在 60×30 面板中心，不读取仍在动画中的 val。
						drawAttributeBar->x.SetTar(
							drawAttributeBar->x.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->y.SetTar(
							drawAttributeBar->y.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->w.SetTar(
							drawAttributeBar->w.tar, operationDur, BarDrawAttributeCompactWidth, true,
							drawAttributeKeyframeValueCurve);
						drawAttributeBar->h.SetTar(
							drawAttributeBar->h.tar, operationDur, BarDrawAttributeCompactHeight, true,
							drawAttributeKeyframeValueCurve);
						drawAttributeBar->rw.value().SetTar(
							drawAttributeBar->rw.value().tar, operationDur,
							CompactDrawAttributeSize(drawAttributeBar->rw.value().tar),
							true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->rh.value().SetTar(
							drawAttributeBar->rh.value().tar, operationDur,
							CompactDrawAttributeSize(drawAttributeBar->rh.value().tar),
							true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->ft.value().SetTar(
							drawAttributeBar->ft.value().tar, operationDur,
							CompactDrawAttributeSize(drawAttributeBar->ft.value().tar),
							true, drawAttributeKeyframeValueCurve);
						drawAttributeBar->pct.SetTar(
							drawAttributeBar->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						drawAttributeBar->framePct.value().SetTar(
							drawAttributeBar->framePct.value().tar, operationDur, 0.0, true,
							drawAttributeKeyframePctCurve);

						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust); i++)
						{
							auto obj = shapeMap[static_cast<BarUISetShapeEnum>(i)];
							if (!obj) continue;
							double middleW = max(1.0, CompactDrawAttributeSize(obj->w.tar));
							double middleH = max(1.0, CompactDrawAttributeSize(obj->h.tar));
							// 根面板负责对齐绘制按钮；子控件保留完整布局的等比微缩坐标。
							double middleX = CompactDrawAttributeX(obj->x.tar);
							double middleY = CompactDrawAttributeY(obj->y.tar);
							obj->x.SetTar(obj->x.tar, operationDur, middleX, true, drawAttributeKeyframeValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur, middleY, true, drawAttributeKeyframeValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur, middleW, true, drawAttributeKeyframeValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur, middleH, true, drawAttributeKeyframeValueCurve);
							if (obj->rw.has_value())
								obj->rw.value().SetTar(obj->rw.value().tar, operationDur,
									CompactDrawAttributeSize(obj->rw.value().tar), true,
									drawAttributeKeyframeValueCurve);
							if (obj->rh.has_value())
								obj->rh.value().SetTar(obj->rh.value().tar, operationDur,
									CompactDrawAttributeSize(obj->rh.value().tar), true,
									drawAttributeKeyframeValueCurve);
							if (obj->ft.has_value())
								obj->ft.value().SetTar(obj->ft.value().tar, operationDur,
									CompactDrawAttributeSize(obj->ft.value().tar), true,
									drawAttributeKeyframeValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
							if (obj->framePct.has_value())
								obj->framePct.value().SetTar(obj->framePct.value().tar, operationDur, 0.0, true,
									drawAttributeKeyframePctCurve);
							if (obj->frameLightPct.has_value())
								obj->frameLightPct.value().SetTar(obj->frameLightPct.value().tar, operationDur, 0.0, true,
									drawAttributeKeyframePctCurve);
						}
						for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust); i++)
						{
							auto obj = svgMap[static_cast<BarUISetSvgEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur,
								CompactDrawAttributeX(obj->x.tar), true, drawAttributeKeyframeValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur,
								CompactDrawAttributeY(obj->y.tar), true, drawAttributeKeyframeValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->w.tar)), true, drawAttributeKeyframeValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->h.tar)), true, drawAttributeKeyframeValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						}
						for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
							i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber); i++)
						{
							auto obj = wordMap[static_cast<BarUISetWordEnum>(i)];
							if (!obj) continue;
							obj->x.SetTar(obj->x.tar, operationDur,
								CompactDrawAttributeX(obj->x.tar), true, drawAttributeKeyframeValueCurve);
							obj->y.SetTar(obj->y.tar, operationDur,
								CompactDrawAttributeY(obj->y.tar), true, drawAttributeKeyframeValueCurve);
							obj->w.SetTar(obj->w.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->w.tar)), true, drawAttributeKeyframeValueCurve);
							obj->h.SetTar(obj->h.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->h.tar)), true, drawAttributeKeyframeValueCurve);
							obj->size.SetTar(obj->size.tar, operationDur,
								max(1.0, CompactDrawAttributeSize(obj->size.tar)), true, drawAttributeKeyframeValueCurve);
							obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						}
						wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->color.SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary), operationDur);
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
				double speedRate = currentAnimationSpeedRate;

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
				double speedRate = !BarUiAnimationEnabled && color.animateWhenDisabled
					? 1.0 : currentAnimationSpeedRate;
				if (forceReplace || startColor == targetColor || !isfinite(duration) || duration <= 0.0
					|| !isfinite(speedRate) || speedRate <= 0.0 || animationDtSeconds <= 0.0)
				{
					FinishColor(color, targetColor);
					return;
				}

				double progress = clamp(static_cast<double>(color.progress) + animationDtSeconds * speedRate / duration, 0.0, 1.0);
				double curveProgress = ApplyAnimationCurve(color.activeCurve, progress,
					color.timelineStartProgress, color.continueTimelinePhase);
			COLORREF nextColor = MixBarUiColor(
				startColor, targetColor, clamp(curveProgress, 0.0, 1.0));
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
				double speedRate = !BarUiAnimationEnabled && pct.animateWhenDisabled
					? 1.0 : currentAnimationSpeedRate;
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
		if (!drawAttributePenPreviewMorph.IsSame())
			ChangeValue(drawAttributePenPreviewMorph, false);
		if (!drawAttributeAnnotationPopupProgress.IsSame())
			ChangeValue(drawAttributeAnnotationPopupProgress, false);
		if (!drawAttributeOverflowPopupProgress.IsSame())
			ChangeValue(drawAttributeOverflowPopupProgress, false);
		if (!drawAttributeBrushPressScale.IsSame()) ChangeValue(drawAttributeBrushPressScale, false);
		if (!drawAttributeHighlightPressScale.IsSame()) ChangeValue(drawAttributeHighlightPressScale, false);
		if (!drawAttributeThicknessFinePressScale.IsSame()) ChangeValue(drawAttributeThicknessFinePressScale, false);
		if (!drawAttributeThicknessMediumPressScale.IsSame()) ChangeValue(drawAttributeThicknessMediumPressScale, false);
		if (!drawAttributeThicknessCoarsePressScale.IsSame()) ChangeValue(drawAttributeThicknessCoarsePressScale, false);
		if (!drawAttributeThicknessAdjustPressScale.IsSame()) ChangeValue(drawAttributeThicknessAdjustPressScale, false);
		if (!drawAttributeAnnotationClosePressScale.IsSame())
			ChangeValue(drawAttributeAnnotationClosePressScale, false);
		if (!drawAttributeOverflowClosePressScale.IsSame())
			ChangeValue(drawAttributeOverflowClosePressScale, false);

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
			if (val->frameLightPct.has_value() && !val->frameLightPct->IsSame()) ChangePct(val->frameLightPct.value(), forceReplace), change = true;
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
			if (val->AdvanceContentTransition(animationDtSeconds, currentAnimationSpeedRate))
				needRendering = true;

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

		auto UpdateHoverAnimation = [&](BarUiPctClass& hoverPct, BarUiColorClass* hoverFill,
			IdtAtomic<BarButtomHoverStageEnum>& hoverStage, bool visible, bool hoverAllowed)
			{
				auto FinishHover = [&]()
					{
						hoverStage = BarButtomHoverStageEnum::None;
						hoverPct.animateWhenDisabled = false;
						if (hoverFill) hoverFill->animateWhenDisabled = false;
					};
				if (!visible)
				{
					// 隐藏时清除仍在运行的独立悬停过程，避免下次显示继承旧的渐隐灰色。
					if (hoverStage != BarButtomHoverStageEnum::None) hoverPct.SetDirect(0.0);
					FinishHover();
				}
				else if (!hoverAllowed)
				{
					// 选中状态继续复用同一背景层，透明度由选中动画接管。
					FinishHover();
				}
				else if (hoverStage == BarButtomHoverStageEnum::Showing
					&& hoverPct.IsSame())
				{
					// 快速显现完成后立即进入独立渐隐阶段，同一次进入不会重新计时。
					hoverStage = BarButtomHoverStageEnum::Fading;
					const BarUiCurveSpecClass hoverFadeCurve{
						BarUiCurveEnum::EaseInSine, BarUiCurveEnum::EaseInSine, 0.0, false };
					hoverPct.SetTar(0.0, BarButtonHoverFadeDur, nullopt, true, hoverFadeCurve);
				}
				else if (hoverStage == BarButtomHoverStageEnum::Fading
					&& hoverPct.IsSame())
				{
					FinishHover();
				}
			};

		auto drawAttributeBrush = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1];
		UpdateHoverAnimation(drawAttributeBrush->pct, &drawAttributeBrush->fill.value(),
			drawAttributeBrushHoverStage, barState.drawAttribute,
			stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1);
		auto drawAttributeHighlight = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1];
		UpdateHoverAnimation(drawAttributeHighlight->pct, &drawAttributeHighlight->fill.value(),
			drawAttributeHighlightHoverStage, barState.drawAttribute,
			stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1);
		const BarUISetShapeEnum thicknessPresetShapes[] =
		{
			BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
		};
		IdtAtomic<BarButtomHoverStageEnum>* thicknessPresetHoverStages[] =
		{
			&drawAttributeThicknessFineHoverStage,
			&drawAttributeThicknessMediumHoverStage,
			&drawAttributeThicknessCoarseHoverStage,
		};
		bool brushMode = stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1;
		int displayedThickness = static_cast<int>(lround(clamp(
			static_cast<double>(drawAttributePenThickness.val), 0.0, 999.0)));
		for (size_t i = 0; i < 3; ++i)
		{
			auto shape = shapeMap[thicknessPresetShapes[i]];
			bool selected = displayedThickness
				== GetBarBrushThicknessPresetPx(i, barStyle.dpiZoom);
			UpdateHoverAnimation(shape->pct, &shape->fill.value(),
				*thicknessPresetHoverStages[i], barState.drawAttribute && brushMode, !selected);
		}
		auto thicknessAdjust =
			shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
		bool thicknessAdjustVisible = barState.drawAttribute && (brushMode
			|| stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1);
		UpdateHoverAnimation(thicknessAdjust->pct, &thicknessAdjust->fill.value(),
			drawAttributeThicknessAdjustHoverStage, thicknessAdjustVisible, true);
		auto annotationClose = shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit];
		UpdateHoverAnimation(annotationClose->pct,
			&annotationClose->fill.value(),
			drawAttributeAnnotationCloseHoverStage,
			barState.drawAttributeBar.thicknessAnnotationPinned, true);
		auto overflowClose = shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit];
		UpdateHoverAnimation(overflowClose->pct,
			&overflowClose->fill.value(),
			drawAttributeOverflowCloseHoverStage,
			barState.drawAttributeBar.thicknessOverflowPinned, true);

		// 特殊体质：按钮
		for (int id = 0; id < barButtomSet.tot; id++)
		{
			BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
			if (temp == nullptr) continue;
			BarUiColorClass* hoverFill = temp->buttom.fill.has_value()
				? &temp->buttom.fill.value() : nullptr;
			UpdateHoverAnimation(temp->buttom.pct, hoverFill, temp->hoverStage,
				!barState.fold && temp->IsVisible(), temp->state->state != BarWidgetState::Selected);
			if (!temp->pressScale.IsSame()) ChangeValue(temp->pressScale, false);

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
				if (temp->buttom.frameLightPct.has_value() && !temp->buttom.frameLightPct->IsSame()) ChangePct(temp->buttom.frameLightPct.value(), forceReplace), change = true;
				if (!temp->buttom.pct.IsSame()) ChangePct(temp->buttom.pct, forceReplace), change = true;
			}
			{
				bool forceReplace = false, change = false;;
				if (temp->icon.forceReplace) temp->icon.forceReplace = false, forceReplace = true;
				if (temp->icon.AdvanceContentTransition(animationDtSeconds, currentAnimationSpeedRate))
					needRendering = true;

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

		// 提示控件全部从动画中的粗细区域派生，换边时随面板收拢到叹号锚点。
		{
			auto mainButton =
				superellipseMap[BarUISetSuperellipseEnum::MainButton];
			mainButton->UpInh(BarUiInheritClass(
				mainButton->x.val - mainButton->w.val / 2.0,
				mainButton->y.val - mainButton->h.val / 2.0));
			auto mainBar = shapeMap[BarUISetShapeEnum::MainBar];
			mainBar->Inherit(BarUiInheritEnum::Center, *mainButton);
			auto drawButton =
				barButtomSet.preset[static_cast<int>(BarButtomPresetEnum::Draw)];
			drawButton->buttom.Inherit(
				BarUiInheritEnum::CenterFromTopLeft, *mainBar);
			auto panel = shapeMap[BarUISetShapeEnum::DrawAttributeBar];
			panel->Inherit(BarUiInheritEnum::Center, drawButton->buttom);
			auto region =
				shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
			region->Inherit(BarUiInheritEnum::TopLeft, *panel);
			auto adjust =
				shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
			BarUiInheritClass adjustInherit =
				adjust->Inherit(BarUiInheritEnum::TopLeft, *panel);
			auto thicknessDisplay =
				wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay];

			double panelScale = panel->w.val / BarDrawAttributeExpandedWidth;
			if (!isfinite(panelScale) || panelScale <= 0.0) panelScale = 0.0;
			double panelExpandedProgress = clamp(
				(panelScale - BarDrawAttributeCompactScale)
				/ (1.0 - BarDrawAttributeCompactScale), 0.0, 1.0);
			double regionCenterY = region->inhY + region->h.val / 2.0;
			double controlCenterY = adjustInherit.y + adjust->h.val / 2.0;
			double controlCenterOffset =
				(BarDrawAttributeThicknessHeight / 2.0
					- BarDrawAttributeGap
					- BarDrawAttributeThicknessControlHeight / 2.0)
				* panelScale;
			double previewSide = controlCenterOffset > 0.000001
				? clamp((regionCenterY - controlCenterY)
					/ controlCenterOffset, -1.0, 1.0)
				: 0.0;
			double previewAreaHeight =
				(BarDrawAttributeThicknessHeight
					- BarDrawAttributeThicknessControlHeight
					- BarDrawAttributeGap * 3.0) * panelScale;
			double previewCenterOffset =
				(BarDrawAttributeThicknessHeight / 2.0
					- BarDrawAttributeGap
					- (BarDrawAttributeThicknessHeight
						- BarDrawAttributeThicknessControlHeight
						- BarDrawAttributeGap * 3.0) / 2.0)
				* panelScale;
			double previewCenterY =
				regionCenterY + previewSide * previewCenterOffset;
			double previewTop = previewCenterY - previewAreaHeight / 2.0;
			// 倒转布局把两个徽标镜像到预览区下沿，换边过程中连续过渡。
			double badgeTopAtUpperEdge =
				previewTop + BarDrawAttributeGap * panelScale;
			double badgeTopAtLowerEdge = previewTop + previewAreaHeight
				- (BarDrawAttributeGap + BarThicknessTooltipBadgeHeight)
					* panelScale;
			double badgeLowerProgress =
				clamp((previewSide + 1.0) / 2.0, 0.0, 1.0);
			double badgeTop = badgeTopAtUpperEdge
				+ (badgeTopAtLowerEdge - badgeTopAtUpperEdge)
					* badgeLowerProgress;
			double badgeMargin =
				BarDrawAttributeThicknessContentInset * panelScale;
			double contentOpacity = clamp(
				static_cast<double>(thicknessDisplay->pct.val), 0.0, 1.0);
			// 徽标显隐跟随当前内容透明度，不能读取收起目标状态后瞬间消失。
			bool annotationVisible =
				PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
			bool overflowVisible = BarThicknessOverflowPreviewAlwaysVisible
				|| barState.drawAttributeBar.thicknessPreviewOverflow;

			auto SetSurfaceDerived = [&](const shared_ptr<BarUiShapeClass>& shape,
				double x, double y, double width, double height, double opacity)
				{
					shape->x.SetDirect(x);
					shape->y.SetDirect(y);
					shape->w.SetDirect(max(0.0, width));
					shape->h.SetDirect(max(0.0, height));
					if (shape->rw.has_value())
						shape->rw->SetDirect(4.0 * panelScale);
					if (shape->rh.has_value())
						shape->rh->SetDirect(4.0 * panelScale);
					if (shape->ft.has_value())
						shape->ft->SetDirect(panelScale);
					shape->pct.SetDirect(
						BarThicknessTooltipFillOpacity * opacity);
					if (shape->framePct.has_value())
						shape->framePct->SetDirect(
							BarThicknessTooltipFrameOpacity * opacity);
					if (shape->frameLightPct.has_value())
						shape->frameLightPct->SetDirect(opacity);
				};
			auto SetHitDerived = [&](const shared_ptr<BarUiShapeClass>& shape,
				double x, double y, double size, bool clearVisual = true)
				{
					shape->x.SetDirect(x);
					shape->y.SetDirect(y);
					shape->w.SetDirect(max(0.0, size));
					shape->h.SetDirect(max(0.0, size));
					if (clearVisual) shape->pct.SetDirect(0.0);
				};

			double annotationOpacity =
				annotationVisible ? contentOpacity : 0.0;
			double annotationBadgeX =
				region->x.val + badgeMargin;
			double annotationBadgeY = badgeTop - panel->inhY;
			double annotationBadgeW = annotationBadgeWidth * panelScale;
			double annotationBadgeH =
				BarThicknessTooltipBadgeHeight * panelScale;
			auto annotationBadge = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationBadge];
			SetSurfaceDerived(annotationBadge,
				annotationBadgeX, annotationBadgeY,
				annotationBadgeW, annotationBadgeH, annotationOpacity);
			annotationBadge->Inherit(BarUiInheritEnum::TopLeft, *panel);

			auto annotationLabel = wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel];
			annotationLabel->x.SetDirect(
				annotationBadgeX + 6.0 * panelScale);
			annotationLabel->y.SetDirect(annotationBadgeY);
			annotationLabel->w.SetDirect(max(0.0,
				annotationBadgeW
					- (6.0 * 2.0 + 4.0 + BarThicknessTooltipIconSize)
						* panelScale));
			annotationLabel->h.SetDirect(annotationBadgeH);
			annotationLabel->size.SetDirect(13.0 * panelScale);
			annotationLabel->pct.SetDirect(annotationOpacity);

			double annotationInfoX = annotationBadgeX + annotationBadgeW
				- (6.0 + BarThicknessTooltipIconSize) * panelScale;
			double annotationInfoY = annotationBadgeY
				+ (BarThicknessTooltipBadgeHeight
					- BarThicknessTooltipIconSize) / 2.0 * panelScale;
			auto annotationInfo = svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo];
			annotationInfo->x.SetDirect(annotationInfoX);
			annotationInfo->y.SetDirect(annotationInfoY);
			annotationInfo->w.SetDirect(
				BarThicknessTooltipIconSize * panelScale);
			annotationInfo->h.SetDirect(
				BarThicknessTooltipIconSize * panelScale);
			annotationInfo->pct.SetDirect(annotationOpacity);
			auto annotationInfoHit = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit];
			double tooltipHitPadding =
				BarThicknessTooltipHitPadding * panelScale;
			SetHitDerived(annotationInfoHit,
				annotationInfoX - tooltipHitPadding,
				annotationInfoY - tooltipHitPadding,
				(BarThicknessTooltipIconSize
					+ BarThicknessTooltipHitPadding * 2.0) * panelScale);
			annotationInfoHit->Inherit(BarUiInheritEnum::TopLeft, *panel);

			double overflowOpacity =
				overflowVisible ? contentOpacity : 0.0;
			double overflowBadgeW =
				BarThicknessTooltipBadgeHeight * panelScale;
			double overflowBadgeX = region->x.val + region->w.val
				- badgeMargin - overflowBadgeW;
			double overflowBadgeY = badgeTop - panel->inhY;
			auto overflowBadge = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge];
			SetSurfaceDerived(overflowBadge,
				overflowBadgeX, overflowBadgeY,
				overflowBadgeW, overflowBadgeW, overflowOpacity);
			overflowBadge->Inherit(BarUiInheritEnum::TopLeft, *panel);

			double overflowInfoX = overflowBadgeX
				+ (BarThicknessTooltipBadgeHeight
					- BarThicknessTooltipIconSize) / 2.0 * panelScale;
			double overflowInfoY = overflowBadgeY
				+ (BarThicknessTooltipBadgeHeight
					- BarThicknessTooltipIconSize) / 2.0 * panelScale;
			auto overflowInfo = svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo];
			overflowInfo->x.SetDirect(overflowInfoX);
			overflowInfo->y.SetDirect(overflowInfoY);
			overflowInfo->w.SetDirect(
				BarThicknessTooltipIconSize * panelScale);
			overflowInfo->h.SetDirect(
				BarThicknessTooltipIconSize * panelScale);
			overflowInfo->pct.SetDirect(overflowOpacity);
			auto overflowInfoHit = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit];
			SetHitDerived(overflowInfoHit,
				overflowInfoX - tooltipHitPadding,
				overflowInfoY - tooltipHitPadding,
				(BarThicknessTooltipIconSize
					+ BarThicknessTooltipHitPadding * 2.0) * panelScale);
			overflowInfoHit->Inherit(BarUiInheritEnum::TopLeft, *panel);

			struct PopupDerivedLayout
			{
				double anchorX = 0.0;
				double anchorY = 0.0;
				double targetLeft = 0.0;
				double targetTop = 0.0;
				double baseWidth = 0.0;
				double baseHeight = 0.0;
				double titleHeight = 0.0;
				double bodyHeight = 0.0;
				double progress = 0.0;
				double opacity = 0.0;
			};
			double logicalWindowWidth = barStyle.zoom > 0.0
				? static_cast<double>(barWindow.w) / barStyle.zoom : 0.0;
			double logicalWindowHeight = barStyle.zoom > 0.0
				? static_cast<double>(barWindow.h) / barStyle.zoom : 0.0;
			auto BuildPopupLayout = [&](double anchorX, double anchorY,
				double width, double height, double titleHeight,
				double bodyHeight, double progress)
				{
					PopupDerivedLayout layout;
					layout.anchorX = anchorX;
					layout.anchorY = anchorY;
					layout.baseWidth = width;
					layout.baseHeight = height;
					layout.titleHeight = titleHeight;
					layout.bodyHeight = bodyHeight;
					layout.progress = max(0.0, progress)
						* panelExpandedProgress;
					layout.opacity = clamp(layout.progress, 0.0, 1.0);
					layout.targetLeft = clamp(
						anchorX - width / 2.0, BarDrawAttributeGap,
						max(BarDrawAttributeGap,
							logicalWindowWidth - BarDrawAttributeGap - width));
					// 浮窗完整落在徽标外侧，避免从叹号中心展开后压住当前按钮。
					double targetCenterY = anchorY + previewSide
						* (BarThicknessTooltipBadgeHeight / 2.0 * panelScale
							+ BarThicknessTooltipPopupGap + height / 2.0);
					layout.targetTop = clamp(
						targetCenterY - height / 2.0,
						BarDrawAttributeGap,
						max(BarDrawAttributeGap,
							logicalWindowHeight - BarDrawAttributeGap - height));
					return layout;
				};

			double annotationAnchorX = panel->inhX + annotationInfoX
				+ BarThicknessTooltipIconSize * panelScale / 2.0;
			double annotationAnchorY = panel->inhY + annotationInfoY
				+ BarThicknessTooltipIconSize * panelScale / 2.0;
			double overflowAnchorX = panel->inhX + overflowInfoX
				+ BarThicknessTooltipIconSize * panelScale / 2.0;
			double overflowAnchorY = panel->inhY + overflowInfoY
				+ BarThicknessTooltipIconSize * panelScale / 2.0;
			PopupDerivedLayout annotationPopupLayout = BuildPopupLayout(
				annotationAnchorX, annotationAnchorY,
				annotationPopupWidth, annotationPopupHeight,
				annotationPopupTitleSize.height,
				annotationPopupBodySize.height,
				drawAttributeAnnotationPopupProgress.val);
			PopupDerivedLayout overflowPopupLayout = BuildPopupLayout(
				overflowAnchorX, overflowAnchorY,
				overflowPopupWidth, overflowPopupHeight,
				overflowPopupTitleSize.height,
				overflowPopupBodySize.height,
				drawAttributeOverflowPopupProgress.val);

			auto ApplyPopupLayout = [&](const PopupDerivedLayout& layout,
				BarUISetShapeEnum popupShapeType,
				BarUISetWordEnum popupTitleType,
				BarUISetWordEnum popupBodyType,
				BarUISetShapeEnum closeHitType,
				BarUISetSvgEnum closeSvgType,
				bool closeVisible)
				{
					double scale = max(0.0, layout.progress);
					double width = layout.baseWidth * scale;
					double height = layout.baseHeight * scale;
					double absoluteX = layout.anchorX
						+ (layout.targetLeft - layout.anchorX) * scale;
					double absoluteY = layout.anchorY
						+ (layout.targetTop - layout.anchorY) * scale;
					double localX = absoluteX - panel->inhX;
					double localY = absoluteY - panel->inhY;
					auto popup = shapeMap[popupShapeType];
					SetSurfaceDerived(popup, localX, localY,
						width, height, layout.opacity);
					// 浮窗从叹号弹性展开时，圆角和边框也按自身比例等比生长。
					if (popup->rw.has_value())
						popup->rw->SetDirect(4.0 * scale);
					if (popup->rh.has_value())
						popup->rh->SetDirect(4.0 * scale);
					if (popup->ft.has_value())
						popup->ft->SetDirect(scale);
					popup->Inherit(BarUiInheritEnum::TopLeft, *panel);

					double padding =
						BarThicknessTooltipPadding * scale;
					double contentWidth = max(0.0,
						width - (BarThicknessTooltipPadding * 2.0
							+ BarThicknessTooltipCloseReserve) * scale);
					auto title = wordMap[popupTitleType];
					title->x.SetDirect(localX + padding);
					title->y.SetDirect(localY + padding);
					title->w.SetDirect(contentWidth);
					title->h.SetDirect(layout.titleHeight * scale);
					title->size.SetDirect(
						BarThicknessTooltipTitleFontSize * scale);
					title->pct.SetDirect(layout.opacity);
					title->Inherit(BarUiInheritEnum::TopLeft, *panel);

					auto body = wordMap[popupBodyType];
					body->x.SetDirect(localX + padding);
					body->y.SetDirect(localY + padding
						+ (layout.titleHeight
							+ BarThicknessTooltipLineGap) * scale);
					body->w.SetDirect(contentWidth);
					body->h.SetDirect(layout.bodyHeight * scale);
					body->size.SetDirect(
						BarThicknessTooltipBodyFontSize * scale);
					body->pct.SetDirect(layout.opacity);
					body->Inherit(BarUiInheritEnum::TopLeft, *panel);

					double closeButtonSize =
						BarThicknessTooltipCloseButtonSize * scale;
					double closeIconSize =
						BarThicknessTooltipIconSize * scale;
					double closeX =
						localX + width - padding - closeButtonSize;
					double closeY = localY + padding;
					auto closeHit = shapeMap[closeHitType];
					SetHitDerived(
						closeHit, closeX, closeY, closeButtonSize, false);
					if (closeHit->rw.has_value())
						closeHit->rw->SetDirect(4.0 * scale);
					if (closeHit->rh.has_value())
						closeHit->rh->SetDirect(4.0 * scale);
					closeHit->Inherit(BarUiInheritEnum::TopLeft, *panel);
					auto closeSvg = svgMap[closeSvgType];
					closeSvg->x.SetDirect(closeX
						+ (closeButtonSize - closeIconSize) / 2.0);
					closeSvg->y.SetDirect(closeY
						+ (closeButtonSize - closeIconSize) / 2.0);
					closeSvg->w.SetDirect(closeIconSize);
					closeSvg->h.SetDirect(closeIconSize);
					closeSvg->pct.SetDirect(
						closeVisible ? layout.opacity : 0.0);
					closeSvg->Inherit(BarUiInheritEnum::TopLeft, *panel);
				};
			auto KeepCloseVisibleThroughCollapse =
				[](bool pinned, const PopupDerivedLayout& layout,
					bool& closeVisible)
				{
					if (pinned) closeVisible = true;
					else if (layout.progress <= 0.000001)
						closeVisible = false;
					return closeVisible;
				};
			ApplyPopupLayout(annotationPopupLayout,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
				BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
				KeepCloseVisibleThroughCollapse(
					barState.drawAttributeBar.thicknessAnnotationPinned,
					annotationPopupLayout,
					drawAttributeAnnotationCloseVisible));
			ApplyPopupLayout(overflowPopupLayout,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
				BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
				KeepCloseVisibleThroughCollapse(
					barState.drawAttributeBar.thicknessOverflowPinned,
					overflowPopupLayout,
					drawAttributeOverflowCloseVisible));
		}

		// 时间轴与属性值在同一帧末尾推进，避免批次剩余时间和实际动画相差一帧。
		mainBarTimeline.Advance(animationDtSeconds, currentAnimationSpeedRate);
		drawAttributeTimeline.Advance(animationDtSeconds, currentAnimationSpeedRate);

	#pragma endregion

		bool needRenderOnce = BarAtomic::renderOnceFlag.exchange(false);
		bool needBorderLightingRendering = spec.PrepareFrameLighting(animationDtSeconds);
		if (needRendering || true == BarAtomic::sustainFlag || true == needRenderOnce
			|| needBorderLightingRendering || BarUiDebugModeEnabled)
		{
		#pragma region 渲染UI

			bool interactiveFrame = needRendering || true == BarAtomic::sustainFlag
				|| needRenderOnce || BarUiDebugModeEnabled;
			auto renderPass = AcquireUi3RenderPass(interactiveFrame
				? Ui3RenderPriority::Interactive : Ui3RenderPriority::Cosmetic);
			if (!renderPass)
			{
				// 其他 UI3 客户端占用共享设备时，装饰帧直接让出本帧且保持 60Hz 节流。
				HighPrecisionWait(chrono::duration<double, milli>(
					chrono::high_resolution_clock::now() - reckon).count(), 60.0);
				reckon = chrono::high_resolution_clock::now();
				continue;
			}

			Ui3RenderDeviceEpoch epoch = GetUi3RenderDeviceEpoch();
			if (epoch.generation != barDeviceGeneration)
			{
				HRESULT hr = CreateBarDeviceResources(epoch);
				if (FAILED(hr))
				{
					if (barDeviceResourceFailureGeneration != epoch.generation)
					{
						barDeviceResourceFailureGeneration = epoch.generation;
						if (IDTLogger) IDTLogger->error(
							"[BarUISetClass::Rendering] 切换 UI3 epoch 后重建 Bar 资源失败, hr=0x{:08X}",
							static_cast<unsigned int>(hr));
					}
					HighPrecisionWait(chrono::duration<double, milli>(
						chrono::high_resolution_clock::now() - reckon).count(), 60.0);
					reckon = chrono::high_resolution_clock::now();
					continue;
				}
				original = RECT(0, 0, barWindow.w, barWindow.h);
			}

			// BeginDraw 前计算三个根控件的保守边界，用同一 dirty rect 约束清除、D2D 和 ULW。
			auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
			mainButton->UpInh(BarUiInheritClass(
				mainButton->x.val - mainButton->w.val / 2.0,
				mainButton->y.val - mainButton->h.val / 2.0));
			auto mainBar = shapeMap[BarUISetShapeEnum::MainBar];
			mainBar->Inherit(BarUiInheritEnum::Center, *mainButton);
			auto drawButton =
				barButtomSet.preset[static_cast<int>(BarButtomPresetEnum::Draw)];
			drawButton->buttom.Inherit(
				BarUiInheritEnum::CenterFromTopLeft, *mainBar);
			auto drawAttribute = shapeMap[BarUISetShapeEnum::DrawAttributeBar];
			drawAttribute->Inherit(BarUiInheritEnum::Center, drawButton->buttom);

			RECT predicted = RECT(0, 0, 0, 0);
			auto IncludeShapeBounds = [&](const shared_ptr<BarUiShapeClass>& shape)
				{
					double lightPct = shape->frameLightPct.has_value()
						? static_cast<double>(shape->frameLightPct.value().val) : 0.0;
					if (shape->enable.val
						&& (shape->pct.val > 0.0 || lightPct > 0.0))
						BarRenderingAttribute::UnionRectInPlace(predicted,
							BarRenderingAttribute::GetWeigetRect(
								*shape, static_cast<double>(barStyle.zoom)));
				};
			auto IncludeSvgBounds = [&](const shared_ptr<BarUiSVGClass>& svg)
				{
					if (svg->enable.val && svg->pct.val > 0.0)
						BarRenderingAttribute::UnionRectInPlace(predicted,
							BarRenderingAttribute::GetWeigetRect(
								*svg, static_cast<double>(barStyle.zoom)));
				};
			auto IncludeWordBounds = [&](const shared_ptr<BarUiWordClass>& word)
				{
					if (word->enable.val && word->pct.val > 0.0)
						BarRenderingAttribute::UnionRectInPlace(predicted,
							BarRenderingAttribute::GetWeigetRect(
								*word, static_cast<double>(barStyle.zoom)));
				};
			IncludeShapeBounds(mainBar);
			IncludeShapeBounds(drawAttribute);
			// 浮窗可越过绘制属性边框，BeginDraw 前必须显式纳入新帧脏区。
			IncludeShapeBounds(shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup]);
			IncludeShapeBounds(shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup]);
			IncludeWordBounds(wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText]);
			IncludeWordBounds(wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody]);
			IncludeWordBounds(wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText]);
			IncludeWordBounds(wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody]);
			IncludeSvgBounds(svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose]);
			IncludeSvgBounds(svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose]);
			if (mainButton->enable.val && mainButton->pct.val > 0.0)
				BarRenderingAttribute::UnionRectInPlace(predicted,
					BarRenderingAttribute::GetWeigetRect(
						*mainButton, static_cast<double>(barStyle.zoom)));

			RECT frameDirty = original;
			BarRenderingAttribute::UnionRectInPlace(frameDirty, predicted);
			if (BarUiDebugModeEnabled)
				frameDirty = RECT(0, 0, barWindow.w, barWindow.h);
			frameDirty.left = clamp<LONG>(
				frameDirty.left, 0, static_cast<LONG>(barWindow.w));
			frameDirty.top = clamp<LONG>(
				frameDirty.top, 0, static_cast<LONG>(barWindow.h));
			frameDirty.right = clamp<LONG>(
				frameDirty.right, 0, static_cast<LONG>(barWindow.w));
			frameDirty.bottom = clamp<LONG>(
				frameDirty.bottom, 0, static_cast<LONG>(barWindow.h));
			if (frameDirty.right <= frameDirty.left
				|| frameDirty.bottom <= frameDirty.top)
				frameDirty = RECT(0, 0, barWindow.w, barWindow.h);
			D2D1_RECT_F frameDirtyRect = D2D1::RectF(
				static_cast<FLOAT>(frameDirty.left),
				static_cast<FLOAT>(frameDirty.top),
				static_cast<FLOAT>(frameDirty.right),
				static_cast<FLOAT>(frameDirty.bottom));

			current = RECT(0, 0, 0, 0);
			barDeviceContext->BeginDraw();
			spec.PushFrameDirtyClip(barDeviceContext.Get(), frameDirtyRect);

			// 清除背景
			{
				D2D1_COLOR_F clearColor = Inkeys::Color::ConvertToD2dColor(RGBA(0, 0, 0, 0));
				// 全局 dirty clip 已经同时覆盖旧、新边界，Clear 不再触碰其余全屏位图。
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
						auto drawAttributePanel = shapeMap[obj];
						double panelGeometryScale = drawAttributePanel->w.val
							/ BarDrawAttributeExpandedWidth;
						if (!isfinite(panelGeometryScale)
							|| panelGeometryScale <= 0.000001)
							panelGeometryScale = 1.0;
						// 等比收拢时复用完整尺寸的柔光遮罩，第三光源亮度全程连续。
						spec.SetFrameDiffuseMaskGeometryScale(
							1.0 / panelGeometryScale);
						spec.Shape(barDeviceContext.Get(), *shapeMap[obj], shapeMap[obj]->Inherit(Center, barButtomSet.preset[(int)BarButtomPresetEnum::Draw]->buttom), &current, true);
						// 只发布三个外层可见区域，Raw Input 高频路径无需遍历全部子控件。
						RefreshBorderCursorVisibleRegions();

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
							struct PenTypeButtonRender
							{
								BarUISetShapeEnum shape;
								BarUISetSvgEnum svg;
								BarUISetWordEnum word;
								BarUiValueClass* pressScale;
							};
							const PenTypeButtonRender penTypeButtons[] =
							{
								{ BarUISetShapeEnum::DrawAttributeBar_Brush2,
									BarUISetSvgEnum::DrawAttributeBar_Brush2,
									BarUISetWordEnum::DrawAttributeBar_Brush2, nullptr },
								{ BarUISetShapeEnum::DrawAttributeBar_Laser,
									BarUISetSvgEnum::DrawAttributeBar_Laser,
									BarUISetWordEnum::DrawAttributeBar_Laser, nullptr },
								{ BarUISetShapeEnum::DrawAttributeBar_Highlight1,
									BarUISetSvgEnum::DrawAttributeBar_Highlight1,
									BarUISetWordEnum::DrawAttributeBar_Highlight1,
									&drawAttributeHighlightPressScale },
								{ BarUISetShapeEnum::DrawAttributeBar_Brush1,
									BarUISetSvgEnum::DrawAttributeBar_Brush1,
									BarUISetWordEnum::DrawAttributeBar_Brush1,
									&drawAttributeBrushPressScale },
								{ BarUISetShapeEnum::DrawAttributeBar_SoftPen,
									BarUISetSvgEnum::DrawAttributeBar_SoftPen,
									BarUISetWordEnum::DrawAttributeBar_SoftPen, nullptr },
							};
							for (const auto& button : penTypeButtons)
							{
								auto shape = shapeMap[button.shape];
								BarUiInheritClass shapeInherit = shape->Inherit(
									TopLeft, *shapeMap[BarUISetShapeEnum::DrawAttributeBar]);
								double pressScale = button.pressScale
									? static_cast<double>(button.pressScale->val) : 1.0;
								if (!isfinite(pressScale) || pressScale <= 0.0) pressScale = 1.0;

								D2D1_MATRIX_3X2_F originalTransform;
								barDeviceContext->GetTransform(&originalTransform);
								bool transformChanged = abs(pressScale - 1.0) > 0.000001;
								if (transformChanged)
								{
									// 与主栏一致，整个图文按钮围绕背景中心缩放。
									FLOAT centerX = static_cast<FLOAT>(
										(shapeInherit.x + shape->w.val / 2.0) * barStyle.zoom);
									FLOAT centerY = static_cast<FLOAT>(
										(shapeInherit.y + shape->h.val / 2.0) * barStyle.zoom);
									D2D1_MATRIX_3X2_F scaleTransform = D2D1::Matrix3x2F::Scale(
										static_cast<FLOAT>(pressScale), static_cast<FLOAT>(pressScale),
										D2D1::Point2F(centerX, centerY));
									barDeviceContext->SetTransform(scaleTransform * originalTransform);
								}

								spec.Shape(barDeviceContext.Get(), *shape, shapeInherit);
								spec.Svg(barDeviceContext.Get(), *svgMap[button.svg],
									svgMap[button.svg]->Inherit(Left, *shape));
								spec.Word(barDeviceContext.Get(), *wordMap[button.word],
									wordMap[button.word]->Inherit(Right, *shape));
								if (transformChanged) barDeviceContext->SetTransform(originalTransform);
							}
						}
						// 粗细调节区域
						{
							auto panel = shapeMap[BarUISetShapeEnum::DrawAttributeBar];
							auto thicknessRegion =
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
							auto thicknessDisplay =
								wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay];
							auto thicknessAdjust =
								shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
							double panelAnimationScale = panel->w.val
								/ BarDrawAttributeExpandedWidth;
							if (!isfinite(panelAnimationScale)
								|| panelAnimationScale <= 0.0)
								panelAnimationScale = 0.0;
							spec.Shape(barDeviceContext.Get(), *thicknessRegion,
								thicknessRegion->Inherit(TopLeft, *panel));
							BarUiInheritClass thicknessDisplayInherit =
								thicknessDisplay->Inherit(TopLeft, *panel);
							BarUiInheritClass thicknessAdjustInherit =
								thicknessAdjust->Inherit(TopLeft, *panel);

							double contentOpacity = thicknessDisplay->pct.val;
							FLOAT uiZoom = static_cast<FLOAT>(barStyle.zoom);
							COLORREF contentColor = thicknessDisplay->color.val;
							if (contentOpacity > 0.000001 && uiZoom > 0.0f)
							{
								// 展开静止后保持真实设备 px；面板动画时只补上同一几何缩放倍率。
								FLOAT requestedThickness = max(0.0f,
									static_cast<FLOAT>(drawAttributePenThickness.val
										* panelAnimationScale));
								// 从动画中的控件位置连续推导上下方向，换边时先收拢到中点再镜像展开。
								double regionCenterY = thicknessRegion->inhY
									+ thicknessRegion->h.val / 2.0;
								double controlCenterY = thicknessAdjustInherit.y
									+ thicknessAdjust->h.val / 2.0;
								double controlCenterOffset =
									(BarDrawAttributeThicknessHeight / 2.0
										- BarDrawAttributeGap
										- BarDrawAttributeThicknessControlHeight / 2.0)
									* panelAnimationScale;
								double previewSide = controlCenterOffset > 0.000001
									? clamp((regionCenterY - controlCenterY)
										/ controlCenterOffset, -1.0, 1.0)
									: 0.0;
								double previewAreaHeight =
									(BarDrawAttributeThicknessHeight
										- BarDrawAttributeThicknessControlHeight
										- BarDrawAttributeGap * 3.0)
									* panelAnimationScale;
								double previewCenterOffset =
									(BarDrawAttributeThicknessHeight / 2.0
										- BarDrawAttributeGap
										- (BarDrawAttributeThicknessHeight
											- BarDrawAttributeThicknessControlHeight
											- BarDrawAttributeGap * 3.0) / 2.0)
									* panelAnimationScale;
								double previewCenterY = regionCenterY
									+ previewSide * previewCenterOffset;
								FLOAT maxPreviewThickness = max(1.0f,
									static_cast<FLOAT>(
										previewAreaHeight * uiZoom));
								FLOAT previewThickness =
									min(requestedThickness, maxPreviewThickness);
								// 水平内边距使用同一面板倍率，避免关键帧读到已经归零的文字局部坐标。
								double horizontalInset =
									BarDrawAttributeThicknessContentInset
									* panelAnimationScale;
								FLOAT left = static_cast<FLOAT>(
									(thicknessRegion->inhX + horizontalInset)
									* uiZoom);
								FLOAT right = static_cast<FLOAT>(
									(thicknessRegion->inhX + thicknessRegion->w.val
										- horizontalInset) * uiZoom);
								FLOAT centerY = static_cast<FLOAT>(
									previewCenterY * uiZoom);
								D2D1_RECT_F previewRect = D2D1::RectF(left,
									centerY - previewThickness / 2.0f, right,
									centerY + previewThickness / 2.0f);
								double previewMorph = clamp(
									static_cast<double>(
										drawAttributePenPreviewMorph.val),
									0.0, 1.0);
								double hardCurveProgress =
									clamp(1.0 - previewMorph * 2.0, 0.0, 1.0);
								double highlighterProgress =
									clamp((previewMorph - 0.5) * 2.0, 0.0, 1.0);
								double panelExpandedProgress = clamp(
									(panelAnimationScale
										- BarDrawAttributeCompactScale)
									/ (1.0 - BarDrawAttributeCompactScale),
									0.0, 1.0);
								ID2D1SolidColorBrush* solidBrush =
									spec.GetFrameSolidColorBrush(
										barDeviceContext.Get(), contentColor,
										contentOpacity);

								if (previewMorph <= 0.5 && solidBrush
									&& previewThickness > 0.0F)
								{
									FLOAT radius = previewThickness / 2.0F;
									FLOAT startX = min(previewRect.right,
										previewRect.left + radius);
									FLOAT endX = max(startX,
										previewRect.right - radius);
									FLOAT span = max(0.0F, endX - startX);
									FLOAT availableAmplitude = max(0.0F,
										(maxPreviewThickness - previewThickness)
											/ 2.0F);
									FLOAT amplitude = min(
										maxPreviewThickness * 0.22F,
										availableAmplitude)
										* static_cast<FLOAT>(
											hardCurveProgress
											* panelExpandedProgress);
									array<D2D1_POINT_2F, 4> points =
									{
										D2D1::Point2F(startX,
											centerY + amplitude),
										D2D1::Point2F(startX + span / 3.0F,
											centerY - amplitude),
										D2D1::Point2F(startX + span * 2.0F / 3.0F,
											centerY + amplitude),
										D2D1::Point2F(endX,
											centerY - amplitude),
									};
									auto path =
										spec.GetThicknessPreviewPath(points);
									auto strokeStyle =
										spec.GetThicknessPreviewStrokeStyle();
									if (path && strokeStyle)
										barDeviceContext->DrawGeometry(
											path, solidBrush, previewThickness,
											strokeStyle);
									else
									{
										// 路径资源失败时保持圆头直线，避免整个预览消失。
										D2D1_ROUNDED_RECT fallback{
											previewRect, radius, radius };
										barDeviceContext->FillRoundedRectangle(
											&fallback, solidBrush);
									}
								}
								else if (previewThickness > 0.0F)
								{
									FLOAT previewRadius =
										previewThickness / 2.0F
										* static_cast<FLOAT>(
											1.0 - highlighterProgress);
									D2D1_ROUNDED_RECT roundedPreview{
										previewRect,
										previewRadius, previewRadius };
									FLOAT leftOpacity = static_cast<FLOAT>(
										1.0 - 0.65 * highlighterProgress);
									ID2D1LinearGradientBrush* gradientBrush =
										spec.GetThicknessPreviewGradientBrush(
											barDeviceContext.Get(), contentColor,
											D2D1::Point2F(
												previewRect.left, centerY),
											D2D1::Point2F(
												previewRect.right, centerY),
											leftOpacity);
									ID2D1Brush* previewBrush =
										gradientBrush
										? static_cast<ID2D1Brush*>(gradientBrush)
										: static_cast<ID2D1Brush*>(solidBrush);
									if (previewBrush)
									{
										if (gradientBrush)
											gradientBrush->SetOpacity(
											static_cast<FLOAT>(contentOpacity));
										barDeviceContext->FillRoundedRectangle(
											&roundedPreview, previewBrush);
									}
								}
							}

							struct ThicknessButtonRender
							{
								BarUISetShapeEnum shape;
								BarUISetWordEnum numberWord;
								BarUiValueClass* pressScale;
								int presetIndex;
							};
							const ThicknessButtonRender thicknessButtons[] =
							{
								{ BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
									BarUISetWordEnum::DrawAttributeBar_ThicknessFineNumber,
									&drawAttributeThicknessFinePressScale, 0 },
								{ BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
									BarUISetWordEnum::DrawAttributeBar_ThicknessMediumNumber,
									&drawAttributeThicknessMediumPressScale, 1 },
								{ BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
									BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber,
									&drawAttributeThicknessCoarsePressScale, 2 },
								{ BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust,
									BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay,
									&drawAttributeThicknessAdjustPressScale, -1 },
							};
							int displayedThickness = static_cast<int>(lround(clamp(
								static_cast<double>(drawAttributePenThickness.val),
								0.0, 999.0)));
							for (const auto& button : thicknessButtons)
							{
								auto shape = shapeMap[button.shape];
								BarUiInheritClass shapeInherit =
									shape->Inherit(TopLeft, *panel);
								bool presetButton = button.presetIndex >= 0;
								auto numberWord = presetButton
									? wordMap[button.numberWord] : nullptr;
								bool adjustVisible = stateMode.Pen.ModeSelect
									== PenModeSelectEnum::IdtPenBrush1
									|| stateMode.Pen.ModeSelect
									== PenModeSelectEnum::IdtPenHighlighter1;
								double buttonOpacity = presetButton
									? static_cast<double>(numberWord->pct.val)
									: (adjustVisible ? contentOpacity : 0.0);
								// 圆点读取 Shape 的当前边框色，跟随白色到青色的已有颜色动画。
								COLORREF buttonColor = shape->frame.value().val;

								double pressScale = button.pressScale->val;
								if (!isfinite(pressScale) || pressScale <= 0.0)
									pressScale = 1.0;
								D2D1_MATRIX_3X2_F originalTransform;
								barDeviceContext->GetTransform(&originalTransform);
								bool transformChanged =
									abs(pressScale - 1.0) > 0.000001;
								if (transformChanged)
								{
									FLOAT centerX = static_cast<FLOAT>(
										(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
									FLOAT centerY = static_cast<FLOAT>(
										(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
									barDeviceContext->SetTransform(
										D2D1::Matrix3x2F::Scale(
											static_cast<FLOAT>(pressScale),
											static_cast<FLOAT>(pressScale),
											D2D1::Point2F(centerX, centerY))
										* originalTransform);
								}

								spec.Shape(barDeviceContext.Get(), *shape, shapeInherit);
								if (!presetButton)
								{
									auto adjustSvg = svgMap[
										BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust];
									D2D1_MATRIX_3X2_F buttonTransform;
									barDeviceContext->GetTransform(&buttonTransform);
									if (barState.widgetPosition.primaryBar)
									{
										// 属性栏换到主栏下方时，调节提示箭头同步朝下。
										FLOAT centerX = static_cast<FLOAT>(
											(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
										FLOAT centerY = static_cast<FLOAT>(
											(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
										barDeviceContext->SetTransform(
											D2D1::Matrix3x2F::Rotation(
												180.0f, D2D1::Point2F(centerX, centerY))
											* buttonTransform);
									}
									spec.Svg(barDeviceContext.Get(), *adjustSvg,
										adjustSvg->Inherit(Center, *shape));
									if (barState.widgetPosition.primaryBar)
										barDeviceContext->SetTransform(buttonTransform);
								}
								else
								{
									ID2D1SolidColorBrush* buttonBrush =
										spec.GetFrameSolidColorBrush(
											barDeviceContext.Get(), buttonColor,
											buttonOpacity);
									if (buttonOpacity > 0.000001 && buttonBrush
										&& uiZoom > 0.0f)
									{
										FLOAT centerX = static_cast<FLOAT>(
											(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
										FLOAT centerY = static_cast<FLOAT>(
											(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
										int actualPx = GetBarBrushThicknessPresetPx(
											button.presetIndex, barStyle.dpiZoom);
										FLOAT innerDiameter = max(1.0f,
											static_cast<FLOAT>(min(
												shape->w.val, shape->h.val) * uiZoom)
											- 8.0f * uiZoom);
										FLOAT diameter = min(
											static_cast<FLOAT>(actualPx
												* panelAnimationScale), innerDiameter);
										D2D1_ELLIPSE ellipse = D2D1::Ellipse(
											D2D1::Point2F(centerX, centerY),
											diameter / 2.0f, diameter / 2.0f);
										barDeviceContext->FillEllipse(
											&ellipse, buttonBrush);
										if (static_cast<FLOAT>(actualPx
											* panelAnimationScale) > innerDiameter)
										{
											// 填满时用黑白高对比数字保留真实设备像素值。
											wstring numberText = to_wstring(actualPx);
											numberWord->content.SetVal(numberText);
											numberWord->content.SetTar(numberText);
											numberWord->color.SetDirect(
												GetBarReadableTextColor(buttonColor));
											spec.Word(barDeviceContext.Get(), *numberWord,
												numberWord->Inherit(TopLeft, *panel),
												DWRITE_FONT_WEIGHT_BOLD,
												DWRITE_TEXT_ALIGNMENT_CENTER);
										}
									}
								}
								if (transformChanged)
									barDeviceContext->SetTransform(originalTransform);
							}

							wstring thicknessText =
								L"粗细 " + to_wstring(displayedThickness);
							thicknessDisplay->content.SetVal(thicknessText);
							thicknessDisplay->content.SetTar(thicknessText);
							spec.Word(barDeviceContext.Get(), *thicknessDisplay,
								thicknessDisplayInherit,
								DWRITE_FONT_WEIGHT_NORMAL,
								DWRITE_TEXT_ALIGNMENT_LEADING);
						}
						spec.SetFrameDiffuseMaskGeometryScale(1.0);
					}

					// 主栏
					auto obj = BarUISetShapeEnum::MainBar;
					spec.Shape(barDeviceContext.Get(), *shapeMap[obj], BarUiInheritClass(shapeMap[obj]->inhX, shapeMap[obj]->inhY), &current, true);

					// 主栏按钮
					for (int id = 0; id < barButtomSet.tot; id++)
					{
						BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
						if (temp == nullptr) continue;

						BarUiInheritClass buttonInherit = temp->buttom.Inherit(
							CenterFromTopLeft, *shapeMap[BarUISetShapeEnum::MainBar]);
						double pressScale = temp->pressScale.val;
						if (!isfinite(pressScale) || pressScale <= 0.0) pressScale = 1.0;
						D2D1_MATRIX_3X2_F originalTransform;
						barDeviceContext->GetTransform(&originalTransform);
						bool transformChanged = abs(pressScale - 1.0) > 0.000001;
						if (transformChanged)
						{
							// 整个按钮组合围绕背景中心缩放，组件自身的布局值和命中区域保持不变。
							FLOAT centerX = static_cast<FLOAT>(
								(buttonInherit.x + temp->buttom.w.val / 2.0) * barStyle.zoom);
							FLOAT centerY = static_cast<FLOAT>(
								(buttonInherit.y + temp->buttom.h.val / 2.0) * barStyle.zoom);
							D2D1_MATRIX_3X2_F scaleTransform = D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(pressScale), static_cast<FLOAT>(pressScale),
								D2D1::Point2F(centerX, centerY));
							barDeviceContext->SetTransform(scaleTransform * originalTransform);
						}

						spec.Shape(barDeviceContext.Get(), temp->buttom, buttonInherit);
						spec.Svg(barDeviceContext.Get(), temp->icon, temp->icon.Inherit(Center, temp->buttom));
						spec.Word(barDeviceContext.Get(), temp->name, temp->name.Inherit(Center, temp->buttom));
						if (transformChanged) barDeviceContext->SetTransform(originalTransform);
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
						i <= static_cast<int>(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit);
						i++)
					{
						auto obj = shapeMap[static_cast<BarUISetShapeEnum>(i)];
						if (obj) BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(*obj, dirtyZoom));
					}
					for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose);
						i++)
					{
						auto obj = svgMap[static_cast<BarUISetSvgEnum>(i)];
						if (obj) BarRenderingAttribute::UnionRectInPlace(
							current, BarRenderingAttribute::GetWeigetRect(*obj, dirtyZoom));
					}
					for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
						i <= static_cast<int>(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody);
						i++)
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
					{
						auto obj = BarUISetSvgEnum::logoInk;
						spec.Svg(barDeviceContext.Get(), *svgMap[obj], svgMap[obj]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]));
					}
				}
			{ /**/ }

			// 浮窗不擦除已经绘制的背景；徽标随后覆盖，形成从按钮下层展开的层级。
			{
				auto panel = shapeMap[BarUISetShapeEnum::DrawAttributeBar];
				if (panel->pct.val > 0.0 && barMedia.formatCache)
				{
					// 属性栏可见期间保留两档完整字号，浮窗首帧只做缓存查询。
					barMedia.formatCache->GetFormat(
						L"HarmonyOS Sans SC",
						static_cast<FLOAT>(
							BarThicknessTooltipTitleFontSize * barStyle.zoom),
						dWriteFontCollection.Get(),
						DWRITE_FONT_WEIGHT_SEMI_BOLD,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						L"zh-cn",
						DWRITE_TEXT_ALIGNMENT_LEADING,
						DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
					barMedia.formatCache->GetFormat(
						L"HarmonyOS Sans SC",
						static_cast<FLOAT>(
							BarThicknessTooltipBodyFontSize * barStyle.zoom),
						dWriteFontCollection.Get(),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						L"zh-cn",
						DWRITE_TEXT_ALIGNMENT_LEADING,
						DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				}
				auto DrawThicknessPopup =
					[&](BarUISetShapeEnum popupShapeType,
						BarUISetWordEnum popupTitleType,
						BarUISetWordEnum popupBodyType,
						BarUISetShapeEnum closeHitType,
						BarUISetSvgEnum closeSvgType,
						BarUISetShapeEnum anchorBadgeType,
						BarUiValueClass& closePressScale)
					{
						auto popup = shapeMap[popupShapeType];
						auto popupTitle = wordMap[popupTitleType];
						auto popupBody = wordMap[popupBodyType];
						auto closeHit = shapeMap[closeHitType];
						auto closeSvg = svgMap[closeSvgType];
						BarUiInheritClass popupInherit =
							popup->Inherit(TopLeft, *panel);
						double popupGeometryScale = popup->rw.has_value()
							? popup->rw->val / 4.0 : 1.0;
						if (!isfinite(popupGeometryScale)
							|| popupGeometryScale <= 0.000001)
							popupGeometryScale = 1.0;
						spec.SetFrameDiffuseMaskGeometryScale(
							1.0 / popupGeometryScale);
						spec.Shape(barDeviceContext.Get(), *popup,
							popupInherit, &current, false);
						spec.SetFrameDiffuseMaskGeometryScale(1.0);

						BarUiInheritClass titleInherit =
							popupTitle->Inherit(TopLeft, *panel);
						BarUiInheritClass bodyInherit =
							popupBody->Inherit(TopLeft, *panel);
						BarUiInheritClass closeHitInherit =
							closeHit->Inherit(TopLeft, *panel);
						BarUiInheritClass closeInherit =
							closeSvg->Inherit(TopLeft, *panel);
						if (popup->pct.val <= 0.0
							|| popupGeometryScale <= 0.000001)
							return;

						auto anchorBadge = shapeMap[anchorBadgeType];
						double anchorX = anchorBadge->inhX
							+ anchorBadge->w.val / 2.0;
						double anchorY = anchorBadge->inhY
							+ anchorBadge->h.val / 2.0;
						auto UnscaleCoordinate =
							[&](double value, double anchor)
							{
								return anchor
									+ (value - anchor) / popupGeometryScale;
							};

						double titleW = popupTitle->w.val;
						double titleH = popupTitle->h.val;
						double titleSize = popupTitle->size.val;
						double bodyW = popupBody->w.val;
						double bodyH = popupBody->h.val;
						double bodySize = popupBody->size.val;
						double closeHitW = closeHit->w.val;
						double closeHitH = closeHit->h.val;
						double closeHitRw = closeHit->rw.has_value()
							? static_cast<double>(closeHit->rw->val) : 0.0;
						double closeHitRh = closeHit->rh.has_value()
							? static_cast<double>(closeHit->rh->val) : 0.0;
						double closeW = closeSvg->w.val;
						double closeH = closeSvg->h.val;
						popupTitle->w.SetDirect(titleW / popupGeometryScale);
						popupTitle->h.SetDirect(titleH / popupGeometryScale);
						popupTitle->size.SetDirect(
							BarThicknessTooltipTitleFontSize);
						popupBody->w.SetDirect(bodyW / popupGeometryScale);
						popupBody->h.SetDirect(bodyH / popupGeometryScale);
						popupBody->size.SetDirect(
							BarThicknessTooltipBodyFontSize);
						closeHit->w.SetDirect(closeHitW / popupGeometryScale);
						closeHit->h.SetDirect(closeHitH / popupGeometryScale);
						if (closeHit->rw.has_value())
							closeHit->rw->SetDirect(
								closeHitRw / popupGeometryScale);
						if (closeHit->rh.has_value())
							closeHit->rh->SetDirect(
								closeHitRh / popupGeometryScale);
						closeSvg->w.SetDirect(closeW / popupGeometryScale);
						closeSvg->h.SetDirect(closeH / popupGeometryScale);

						D2D1_MATRIX_3X2_F originalTransform;
						barDeviceContext->GetTransform(&originalTransform);
						barDeviceContext->SetTransform(
							D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(popupGeometryScale),
								static_cast<FLOAT>(popupGeometryScale),
								D2D1::Point2F(
									static_cast<FLOAT>(anchorX * barStyle.zoom),
									static_cast<FLOAT>(anchorY * barStyle.zoom)))
							* originalTransform);
						// 文字和 SVG 使用完整尺寸资源再整体缩放，避免动画每帧创建新字号格式。
						spec.Word(barDeviceContext.Get(), *popupTitle,
							BarUiInheritClass(
								UnscaleCoordinate(titleInherit.x, anchorX),
								UnscaleCoordinate(titleInherit.y, anchorY)),
							DWRITE_FONT_WEIGHT_SEMI_BOLD,
							DWRITE_TEXT_ALIGNMENT_LEADING);
						spec.Word(barDeviceContext.Get(), *popupBody,
							BarUiInheritClass(
								UnscaleCoordinate(bodyInherit.x, anchorX),
								UnscaleCoordinate(bodyInherit.y, anchorY)),
							DWRITE_FONT_WEIGHT_NORMAL,
							DWRITE_TEXT_ALIGNMENT_LEADING);
						double unscaledCloseX =
							UnscaleCoordinate(closeHitInherit.x, anchorX);
						double unscaledCloseY =
							UnscaleCoordinate(closeHitInherit.y, anchorY);
						double closeScale = closePressScale.val;
						D2D1_MATRIX_3X2_F popupTransform;
						barDeviceContext->GetTransform(&popupTransform);
						if (abs(closeScale - 1.0) > 0.000001)
						{
							// 背景与叉号围绕 20px 按钮中心同步缩放，命中区域保持原尺寸。
							double closeCenterX =
								unscaledCloseX + closeHit->w.val / 2.0;
							double closeCenterY =
								unscaledCloseY + closeHit->h.val / 2.0;
							barDeviceContext->SetTransform(
								D2D1::Matrix3x2F::Scale(
									static_cast<FLOAT>(closeScale),
									static_cast<FLOAT>(closeScale),
									D2D1::Point2F(
										static_cast<FLOAT>(
											closeCenterX * barStyle.zoom),
										static_cast<FLOAT>(
											closeCenterY * barStyle.zoom)))
								* popupTransform);
						}
						spec.Shape(barDeviceContext.Get(), *closeHit,
							BarUiInheritClass(unscaledCloseX, unscaledCloseY));
						spec.Svg(barDeviceContext.Get(), *closeSvg,
							BarUiInheritClass(
								UnscaleCoordinate(closeInherit.x, anchorX),
								UnscaleCoordinate(closeInherit.y, anchorY)));
						barDeviceContext->SetTransform(originalTransform);

						popupTitle->w.SetDirect(titleW);
						popupTitle->h.SetDirect(titleH);
						popupTitle->size.SetDirect(titleSize);
						popupBody->w.SetDirect(bodyW);
						popupBody->h.SetDirect(bodyH);
						popupBody->size.SetDirect(bodySize);
						closeHit->w.SetDirect(closeHitW);
						closeHit->h.SetDirect(closeHitH);
						if (closeHit->rw.has_value())
							closeHit->rw->SetDirect(closeHitRw);
						if (closeHit->rh.has_value())
							closeHit->rh->SetDirect(closeHitRh);
						closeSvg->w.SetDirect(closeW);
						closeSvg->h.SetDirect(closeH);
					};
				DrawThicknessPopup(
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
					BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
					BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
					BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationBadge,
					drawAttributeAnnotationClosePressScale);
				DrawThicknessPopup(
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
					BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
					BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
					BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge,
					drawAttributeOverflowClosePressScale);

				auto annotationBadge = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationBadge];
				double badgeGeometryScale = annotationBadge->rw.has_value()
					? annotationBadge->rw->val / 4.0 : 1.0;
				if (!isfinite(badgeGeometryScale)
					|| badgeGeometryScale <= 0.000001)
					badgeGeometryScale = 1.0;
				spec.SetFrameDiffuseMaskGeometryScale(
					1.0 / badgeGeometryScale);
				spec.Shape(barDeviceContext.Get(), *annotationBadge,
					annotationBadge->Inherit(TopLeft, *panel));
				auto annotationLabel = wordMap[
					BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel];
				spec.Word(barDeviceContext.Get(), *annotationLabel,
					annotationLabel->Inherit(TopLeft, *panel),
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_TEXT_ALIGNMENT_LEADING);
				auto annotationInfo = svgMap[
					BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo];
				spec.Svg(barDeviceContext.Get(), *annotationInfo,
					annotationInfo->Inherit(TopLeft, *panel));

				auto overflowBadge = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge];
				spec.Shape(barDeviceContext.Get(), *overflowBadge,
					overflowBadge->Inherit(TopLeft, *panel));
				auto overflowInfo = svgMap[
					BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo];
				spec.Svg(barDeviceContext.Get(), *overflowInfo,
					overflowInfo->Inherit(TopLeft, *panel));
				spec.SetFrameDiffuseMaskGeometryScale(1.0);
			}

			// 调试模式持续显示实时 FPS，并把文本范围加入脏区。
			if (BarUiDebugModeEnabled)
			{
				FLOAT tarZoom = static_cast<FLOAT>(barStyle.zoom);
				wstring content = L"开发版本 " + editionDate + L" | 不代表最终品质 | " + fps;

				ComPtr<IDWriteTextFormat> pTextFormat;
				pTextFormat = barMedia.formatCache->GetFormat(
					L"HarmonyOS Sans SC",
					12.0F * tarZoom,
					dWriteFontCollection.Get(),
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					L"zh-cn",
					DWRITE_TEXT_ALIGNMENT_LEADING, // 指定文本左对齐
					DWRITE_PARAGRAPH_ALIGNMENT_NEAR // 指定段落顶部对齐
				);

				// 3. 创建画刷
				ID2D1SolidColorBrush* pBrush =
					spec.GetFrameSolidColorBrush(
						barDeviceContext.Get(), RGB(255, 255, 255), 0.5);

				double tarX = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->inhX;
				double tarY = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->inhY + barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetH();

				// 4. 设定绘制区域
				D2D1_RECT_F layoutRect = D2D1::RectF(
					static_cast<FLOAT>(tarX * tarZoom), static_cast<FLOAT>(tarY * tarZoom),
					static_cast<FLOAT>((tarX + 300) * tarZoom),
					static_cast<FLOAT>((tarY + 20) * tarZoom));

				RECT tmp = RECT((LONG)(layoutRect.left), (LONG)(layoutRect.top), (LONG)(layoutRect.right), (LONG)(layoutRect.bottom));
				BarRenderingAttribute::UnionRectInPlace(current, tmp);

				// 5. 绘制文本
				if (pBrush) barDeviceContext->DrawTextW(
					content.c_str(),           // text
					(UINT32)content.length(),  // text length
					pTextFormat.Get(),         // format
					layoutRect,                // layout rect
					pBrush,                    // brush
					D2D1_DRAW_TEXT_OPTIONS_NONE
				);
			}

			if (BarUiDebugModeEnabled)
			{
				// 红框只标记本帧即将提交的实际脏区，不改变正常更新区域。
				RECT debugTarget = original;
				BarRenderingAttribute::UnionRectInPlace(debugTarget, current);
				{
					if (debugTarget.left < 0) debugTarget.left = 0;
					if (debugTarget.top < 0) debugTarget.top = 0;
					LONG debugWindowWidth = static_cast<LONG>(barWindow.w);
					LONG debugWindowHeight = static_cast<LONG>(barWindow.h);
					if (debugTarget.right > debugWindowWidth) debugTarget.right = debugWindowWidth;
					if (debugTarget.bottom > debugWindowHeight) debugTarget.bottom = debugWindowHeight;
				}

				COLORREF frame = RGB(255, 0, 0);
				D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
					static_cast<FLOAT>(debugTarget.left), static_cast<FLOAT>(debugTarget.top),
					static_cast<FLOAT>(debugTarget.right - 1),
					static_cast<FLOAT>(debugTarget.bottom - 1)), 0, 0);

				ID2D1SolidColorBrush* borderBrush =
					spec.GetFrameSolidColorBrush(
						barDeviceContext.Get(), frame, 1.0);

				if (borderBrush)
					barDeviceContext->DrawRoundedRectangle(
						&roundedRect, borderBrush, 1.0f);
			}

			// Windows 7 Platform Update 要求 GetDC 时 Clip/Layer 栈为空。
			spec.PopFrameDirtyClip(barDeviceContext.Get());
			{
				// 脏区更新
				RECT target = frameDirty;
				original = current;
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
					// GetDC 自带必要的 D2D 提交，避免在此之前再做一次重复 Flush。
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

			HRESULT endDrawHr = barDeviceContext->EndDraw();
			spec.HandleFrameEndDrawResult(endDrawHr);
			if (FAILED(endDrawHr))
			{
				if (!barEndDrawFailureLogged && IDTLogger)
					IDTLogger->error(
						"[BarUISetClass::Rendering] EndDraw 失败，将在下一帧降级恢复或重建设备资源, hr=0x{:08X}",
						static_cast<unsigned int>(endDrawHr));
				barEndDrawFailureLogged = true;
				if (endDrawHr == D2DERR_RECREATE_TARGET)
				{
					barDeviceGeneration = 0;
					spec.DiscardDeviceResources();
				}
				BarAtomic::renderOnceFlag = true;
			}
			else barEndDrawFailureLogged = false;
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

		if (BarUiDebugModeEnabled)
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
	BarButtomClass* hoveredMainBarButton = nullptr;
	bool suppressHoverUntilPointerMove = false;
	POINT hoverSuppressionScreenPoint{};
	enum class IndependentHoverTargetEnum
	{
		None,
		DrawAttributeBrush,
		DrawAttributeHighlight,
		DrawAttributeThicknessFine,
		DrawAttributeThicknessMedium,
		DrawAttributeThicknessCoarse,
		DrawAttributeThicknessAdjust,
		DrawAttributeAnnotationClose,
		DrawAttributeOverflowClose,
	};
	IndependentHoverTargetEnum hoveredIndependentButton = IndependentHoverTargetEnum::None;
	struct HoverVisualRef
	{
		BarUiPctClass* pct = nullptr;
		BarUiColorClass* fill = nullptr;
		IdtAtomic<BarButtomHoverStageEnum>* stage = nullptr;
	};
	auto GetIndependentHoverVisual = [&](IndependentHoverTargetEnum target) -> HoverVisualRef
		{
			switch (target)
			{
			case IndependentHoverTargetEnum::DrawAttributeBrush:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->fill.value(),
					&drawAttributeBrushHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeHighlight:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]->fill.value(),
					&drawAttributeHighlightHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeThicknessFine:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessFine]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessFine]->fill.value(),
					&drawAttributeThicknessFineHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeThicknessMedium:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium]->fill.value(),
					&drawAttributeThicknessMediumHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeThicknessCoarse:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse]->fill.value(),
					&drawAttributeThicknessCoarseHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeThicknessAdjust:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust]->fill.value(),
					&drawAttributeThicknessAdjustHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeAnnotationClose:
				return { &shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit]->pct,
					&shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit]
						->fill.value(),
					&drawAttributeAnnotationCloseHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeOverflowClose:
				return { &shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit]->pct,
					&shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit]
						->fill.value(),
					&drawAttributeOverflowCloseHoverStage };
			default:
				return {};
			}
		};
	auto StartHover = [&](BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtomHoverStageEnum>* hoverStage)
		{
			if (!hoverPct || !hoverFill || !hoverStage) return;
			hoverPct->animateWhenDisabled = true;
			hoverFill->animateWhenDisabled = true;
			const BarUiCurveSpecClass hoverShowCurve{
				BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
			hoverFill->SetTar(GetThemeColor(BarThemeColorEnum::PressedFill),
				BarButtonHoverShowDur, hoverShowCurve);
			hoverPct->SetTar(
				BarButtonHoverOpacity, BarButtonHoverShowDur, nullopt, true, hoverShowCurve);
			*hoverStage = BarButtomHoverStageEnum::Showing;
			UpdateRendering(false);
		};
	auto StopHover = [&](BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtomHoverStageEnum>* hoverStage, bool immediate,
		bool preserveVisual = false)
		{
			if (!hoverPct || !hoverStage) return;
			if (immediate)
			{
				*hoverStage = BarButtomHoverStageEnum::None;
				// 按下时保留当前视觉值交给按压态续接，隐藏等场景仍立即清零。
				if (!preserveVisual) hoverPct->SetDirect(0.0);
				hoverPct->animateWhenDisabled = false;
				if (hoverFill) hoverFill->animateWhenDisabled = false;
			}
			else
			{
				// 离开后仍保持灰色背景，直到同一层透明度自然降为零。
				hoverPct->animateWhenDisabled = true;
				if (hoverFill) hoverFill->animateWhenDisabled = true;
				*hoverStage = BarButtomHoverStageEnum::Fading;
				const BarUiCurveSpecClass hoverExitCurve{
					BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
			hoverPct->SetTar(0.0, BarButtonHoverExitDur, nullopt, true, hoverExitCurve);
			}
			UpdateRendering(false);
		};
	auto StartMainBarButtonHover = [&](BarButtomClass* button)
		{
			if (button && button->buttom.fill.has_value())
				StartHover(&button->buttom.pct, &button->buttom.fill.value(), &button->hoverStage);
		};
	auto StopMainBarButtonHover = [&](BarButtomClass* button, bool immediate, bool preserveVisual = false)
		{
			if (button) StopHover(&button->buttom.pct,
				button->buttom.fill.has_value() ? &button->buttom.fill.value() : nullptr,
				&button->hoverStage, immediate, preserveVisual);
		};
	auto StartIndependentHover = [&](IndependentHoverTargetEnum target)
		{
			auto hover = GetIndependentHoverVisual(target);
			StartHover(hover.pct, hover.fill, hover.stage);
		};
	auto StopIndependentHover = [&](IndependentHoverTargetEnum target, bool immediate,
		bool preserveVisual = false)
		{
			auto hover = GetIndependentHoverVisual(target);
			// 与主栏一致：移出快速退出，按下时从当前悬停视觉连续衔接。
			StopHover(hover.pct, hover.fill, hover.stage, immediate, preserveVisual);
		};
	auto SuppressHoverUntilPointerMove = [&]()
		{
			POINT point{};
			if (GetCursorPos(&point))
			{
				hoverSuppressionScreenPoint = point;
				suppressHoverUntilPointerMove = true;
			}
		};
	auto IsIndependentHoverAllowed = [&](IndependentHoverTargetEnum target)
		{
			if (!barState.drawAttribute) return false;
			switch (target)
			{
			case IndependentHoverTargetEnum::DrawAttributeBrush:
				return stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1;
			case IndependentHoverTargetEnum::DrawAttributeHighlight:
				return stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1;
			case IndependentHoverTargetEnum::DrawAttributeThicknessFine:
			case IndependentHoverTargetEnum::DrawAttributeThicknessMedium:
			case IndependentHoverTargetEnum::DrawAttributeThicknessCoarse:
			{
				if (stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1)
					return false;
				size_t index = static_cast<size_t>(target)
					- static_cast<size_t>(
						IndependentHoverTargetEnum::DrawAttributeThicknessFine);
				int displayedThickness =
					static_cast<int>(lround(max(0.0f, GetPenWidth())));
				return displayedThickness
					!= GetBarBrushThicknessPresetPx(index, barStyle.dpiZoom);
			}
			case IndependentHoverTargetEnum::DrawAttributeThicknessAdjust:
				return stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1
					|| stateMode.Pen.ModeSelect
					== PenModeSelectEnum::IdtPenHighlighter1;
			case IndependentHoverTargetEnum::DrawAttributeAnnotationClose:
				return !barState.fold
					&& barState.drawAttributeBar.thicknessAnnotationPinned
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect);
			case IndependentHoverTargetEnum::DrawAttributeOverflowClose:
				return !barState.fold
					&& barState.drawAttributeBar.thicknessOverflowPinned
					&& barState.drawAttributeBar.thicknessPreviewOverflow;
			default:
				return false;
			}
		};
	auto SetTooltipFlag = [&](IdtAtomic<bool>& flag, bool value)
		{
			if (static_cast<bool>(flag) == value) return false;
			flag = value;
			return true;
		};
	auto AnnotationTooltipAvailable = [&]()
		{
			return barState.drawAttribute && !barState.fold
				&& PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
		};
	auto OverflowTooltipAvailable = [&]()
		{
			return barState.drawAttribute && !barState.fold
				&& barState.drawAttributeBar.thicknessPreviewOverflow;
		};
	while (!offSignal)
	{
		hiex::getmessage_win32(&msg, EM_MOUSE, floating_window);
		if (hoveredMainBarButton
			&& (barState.fold || !hoveredMainBarButton->IsVisible()))
		{
			StopMainBarButtonHover(hoveredMainBarButton, true);
			hoveredMainBarButton = nullptr;
		}
		if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
		{
			if (!IsIndependentHoverAllowed(hoveredIndependentButton))
			{
				StopIndependentHover(hoveredIndependentButton, true);
				hoveredIndependentButton = IndependentHoverTargetEnum::None;
			}
		}
		if (msg.message == WM_MOUSEMOVE)
		{
			if (suppressHoverUntilPointerMove)
			{
				POINT currentPoint{};
				if (GetCursorPos(&currentPoint)
					&& currentPoint.x == hoverSuppressionScreenPoint.x
					&& currentPoint.y == hoverSuppressionScreenPoint.y)
				{
					// 窗口伸缩会产生相对坐标变化，但屏幕坐标未变时不算重新进入按钮。
					continue;
				}
				suppressHoverUntilPointerMove = false;
			}

			bool tooltipHoverChanged = false;
			auto annotationInfoHit = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit];
			auto overflowInfoHit = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit];
			tooltipHoverChanged |= SetTooltipFlag(
				barState.drawAttributeBar.thicknessAnnotationHover,
				AnnotationTooltipAvailable() && annotationInfoHit
				&& annotationInfoHit->IsClick(
					msg.x, msg.y, barStyle.zoom));
			tooltipHoverChanged |= SetTooltipFlag(
				barState.drawAttributeBar.thicknessOverflowHover,
				OverflowTooltipAvailable() && overflowInfoHit
				&& overflowInfoHit->IsClick(
					msg.x, msg.y, barStyle.zoom));
			if (tooltipHoverChanged) UpdateRendering(false);

			BarButtomClass* currentHoveredButton = nullptr;
			if (!barState.fold)
			{
				for (int id = 0; id < barButtomSet.tot; id++)
				{
					BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
					if (!temp || !temp->IsVisible() || temp->state->state == BarWidgetState::Selected) continue;
					bool isColorSelector = temp->name.enable.tar
						&& temp->name.content.GetTar().substr(0, 7) == L"__color";
					if (isColorSelector) continue; // 颜色块自身就是内容，不把其填充色改成悬停灰色。
					if (temp->buttom.IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentHoveredButton = temp;
						break;
					}
				}
			}
			if (currentHoveredButton != hoveredMainBarButton)
			{
				StopMainBarButtonHover(hoveredMainBarButton, false);
				hoveredMainBarButton = currentHoveredButton;
				StartMainBarButtonHover(hoveredMainBarButton);
			}

			IndependentHoverTargetEnum currentIndependentButton = IndependentHoverTargetEnum::None;
			if (barState.drawAttribute)
			{
				// 两个浮窗允许覆盖，按绘制顺序优先命中上层的粗细超限浮窗。
				auto overflowClose = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit];
				auto annotationClose = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit];
				if (IsIndependentHoverAllowed(
					IndependentHoverTargetEnum::DrawAttributeOverflowClose)
					&& overflowClose
					&& overflowClose->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					currentIndependentButton =
						IndependentHoverTargetEnum::DrawAttributeOverflowClose;
				}
				else if (IsIndependentHoverAllowed(
					IndependentHoverTargetEnum::DrawAttributeAnnotationClose)
					&& annotationClose
					&& annotationClose->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					currentIndependentButton =
						IndependentHoverTargetEnum::DrawAttributeAnnotationClose;
				}

				const BarUISetShapeEnum presetShapes[] =
				{
					BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
				};
				for (size_t i = 0;
					currentIndependentButton == IndependentHoverTargetEnum::None
						&& i < 3;
					++i)
				{
					auto target = static_cast<IndependentHoverTargetEnum>(
						static_cast<int>(
							IndependentHoverTargetEnum::DrawAttributeThicknessFine)
						+ static_cast<int>(i));
					auto obj = shapeMap[presetShapes[i]];
					if (IsIndependentHoverAllowed(target)
						&& obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton = target;
						break;
					}
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None)
				{
					auto obj =
						shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
					if (IsIndependentHoverAllowed(
						IndependentHoverTargetEnum::DrawAttributeThicknessAdjust)
						&& obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeThicknessAdjust;
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None)
				{
					if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1];
					stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1
					&& obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeBrush;
					}
					else if (auto obj =
						shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1];
						stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1
						&& obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeHighlight;
					}
				}
			}
			if (currentIndependentButton != hoveredIndependentButton)
			{
				StopIndependentHover(hoveredIndependentButton, false);
				hoveredIndependentButton = currentIndependentButton;
				StartIndependentHover(hoveredIndependentButton);
			}
		}

		{
			bool continueFlag = true;

			// 提示控件位于顶层，固定浮窗必须先于下方主栏和属性栏消费点击。
			struct ThicknessTooltipInteraction
			{
				BarUISetShapeEnum infoHit;
				BarUISetShapeEnum popup;
				BarUISetShapeEnum closeHit;
				IdtAtomic<bool>* hover;
				IdtAtomic<bool>* pinned;
				IdtAtomic<bool>* pressed;
				bool available;
			};
			ThicknessTooltipInteraction tooltipInteractions[] =
			{
				{
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
					&barState.drawAttributeBar.thicknessOverflowHover,
					&barState.drawAttributeBar.thicknessOverflowPinned,
					&barState.drawAttributeBar.thicknessOverflowClosePress,
					OverflowTooltipAvailable(),
				},
				{
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
					&barState.drawAttributeBar.thicknessAnnotationHover,
					&barState.drawAttributeBar.thicknessAnnotationPinned,
					&barState.drawAttributeBar.thicknessAnnotationClosePress,
					AnnotationTooltipAvailable(),
				},
			};

			for (auto& tooltip : tooltipInteractions)
			{
				auto closeHit = shapeMap[tooltip.closeHit];
				if (!continueFlag || !tooltip.available
					|| !static_cast<bool>(*tooltip.pinned) || !closeHit
					|| !closeHit->IsClick(msg.x, msg.y, barStyle.zoom))
					continue;

				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					*tooltip.pressed = true;
					StopIndependentHover(
						hoveredIndependentButton, true, true);
					hoveredIndependentButton =
						IndependentHoverTargetEnum::None;
					UpdateRendering(false);
					while (true)
					{
						hiex::getmessage_win32(
							&msg, EM_MOUSE, floating_window);
						if (closeHit->IsClick(
							msg.x, msg.y, barStyle.zoom))
						{
							if (!msg.lbutton)
							{
								*tooltip.pinned = false;
								*tooltip.hover = false;
								break;
							}
						}
						else break;
					}
					*tooltip.pressed = false;
					UpdateRendering(false);
					SuppressHoverUntilPointerMove();
					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}
			}

			for (auto& tooltip : tooltipInteractions)
			{
				auto infoHit = shapeMap[tooltip.infoHit];
				if (!continueFlag || !tooltip.available || !infoHit
					|| !infoHit->IsClick(msg.x, msg.y, barStyle.zoom))
					continue;

				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					while (true)
					{
						hiex::getmessage_win32(
							&msg, EM_MOUSE, floating_window);
						if (infoHit->IsClick(
							msg.x, msg.y, barStyle.zoom))
						{
							if (!msg.lbutton)
							{
								*tooltip.pinned = true;
								// 悬停浮窗已经稳定时没有进度动画，固定后强制补一帧显示关闭按钮。
								BarAtomic::renderOnceFlag = true;
								break;
							}
						}
						else break;
					}
					// 固定态接管可见性；拖出取消时也清掉本次 hover。
					*tooltip.hover = false;
					UpdateRendering(false);
					SuppressHoverUntilPointerMove();
					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}
			}

			for (auto& tooltip : tooltipInteractions)
			{
				auto popup = shapeMap[tooltip.popup];
				if (continueFlag && tooltip.available
					&& static_cast<bool>(*tooltip.pinned) && popup
					&& popup->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					// 固定浮窗正文只阻止点击穿透，关闭动作由右上角 X 独立处理。
					continueFlag = false;
				}
			}

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
					SuppressHoverUntilPointerMove();

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
					if (temp == nullptr || !temp->IsVisible()) continue;

					// 双击第二击仍归属于第一击按钮，避免动画中按钮位移导致命中丢失。
					bool doubleClickContinuation = msg.message == WM_LBUTTONDBLCLK
						&& temp == lastClickedMainBarButton;
					if (temp->buttom.IsClick(msg.x, msg.y, barStyle.zoom) || doubleClickContinuation)
					{
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK)
						{
							bool clickCompleted = false;
							// 同一背景层先切换到按下状态；抬起后必须收到新的鼠标移动才能再次悬停。
							temp->state->emph = BarWidgetEmphasize::Pressed;
							StopMainBarButtonHover(hoveredMainBarButton, true, true);
							hoveredMainBarButton = nullptr;
							UpdateRendering(false);
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
							SuppressHoverUntilPointerMove();

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

								SuppressHoverUntilPointerMove();
								hiex::flushmessage_win32(EM_MOUSE, floating_window);
							}
						}

						if (!continueFlag) break;
					}
				}

				// 粗细预设和未来调节入口
				if (continueFlag && barState.drawAttribute)
				{
					struct ThicknessButtonInteraction
					{
						BarUISetShapeEnum shape;
						IdtAtomic<bool>* pressed;
						int presetIndex;
					};
					const ThicknessButtonInteraction thicknessButtons[] =
					{
						{ BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
							&barState.drawAttributeBar.thicknessFinePress, 0 },
						{ BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
							&barState.drawAttributeBar.thicknessMediumPress, 1 },
						{ BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
							&barState.drawAttributeBar.thicknessCoarsePress, 2 },
						{ BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust,
							&barState.drawAttributeBar.thicknessAdjustPress, -1 },
					};
					bool brushMode =
						stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1;
					for (const auto& button : thicknessButtons)
					{
						bool visible = button.presetIndex >= 0 ? brushMode
							: (brushMode || stateMode.Pen.ModeSelect
								== PenModeSelectEnum::IdtPenHighlighter1);
						auto obj = shapeMap[button.shape];
						if (!visible || !obj
							|| !obj->IsClick(msg.x, msg.y, barStyle.zoom))
							continue;

						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN)
						{
							*button.pressed = true;
							StopIndependentHover(hoveredIndependentButton, true, true);
							hoveredIndependentButton =
								IndependentHoverTargetEnum::None;
							UpdateRendering(false);
							while (true)
							{
								hiex::getmessage_win32(
									&msg, EM_MOUSE, floating_window);
								if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
								{
									if (!msg.lbutton)
									{
										if (button.presetIndex >= 0)
											SetPenWidth(static_cast<float>(
												GetBarBrushThicknessPresetPx(
													button.presetIndex,
													barStyle.dpiZoom)));
										UpdateRendering();
										break;
									}
								}
								else break;
							}
							*button.pressed = false;
							UpdateRendering(false);
							SuppressHoverUntilPointerMove();
							hiex::flushmessage_win32(
								EM_MOUSE, floating_window);
						}
						break;
					}
				}

				// 画笔
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.drawAttributeBar.brush1Press = true;
						StopIndependentHover(hoveredIndependentButton, true, true);
						hoveredIndependentButton = IndependentHoverTargetEnum::None;
						UpdateRendering(false);
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
						SuppressHoverUntilPointerMove();

						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
				// 荧光笔
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.drawAttributeBar.highlight1Press = true;
						StopIndependentHover(hoveredIndependentButton, true, true);
						hoveredIndependentButton = IndependentHoverTargetEnum::None;
						UpdateRendering(false);
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
						SuppressHoverUntilPointerMove();

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

bool BarUISetClass::SetBorderCursorRawInputEnabled(HWND hWnd, bool enabled)
{
	if (!hWnd) return false;
	if (!enabled)
	{
		bool wasRegistered = false;
		{
			lock_guard lock(borderCursorLightMutex);
			wasRegistered = borderCursorRawInputRegistered;
			// 先关闭逻辑入口，注销失败时迟到的 WM_INPUT 也不会继续唤醒渲染。
			borderCursorRawInputRegistered = false;
			borderCursorInputAvailable = false;
		}
		if (!wasRegistered) return true;

		RAWINPUTDEVICE rawInputDevice{};
		rawInputDevice.usUsagePage = 0x01;
		rawInputDevice.usUsage = 0x02;
		rawInputDevice.dwFlags = RIDEV_REMOVE;
		rawInputDevice.hwndTarget = nullptr;
		if (RegisterRawInputDevices(&rawInputDevice, 1, sizeof(rawInputDevice))) return true;

		DWORD removalError = GetLastError();
		bool needLog = false;
		{
			lock_guard lock(borderCursorLightMutex);
			if (!borderCursorRemovalFailureLogged)
			{
				borderCursorRemovalFailureLogged = true;
				needLog = true;
			}
		}
		if (needLog && IDTLogger) IDTLogger->error(
			"[BarUISetClass::SetBorderCursorRawInputEnabled] 注销全局鼠标 Raw Input 失败, error={}",
			removalError);
		return false;
	}

	{
		lock_guard lock(borderCursorLightMutex);
		if (borderCursorRawInputRegistered) return true;
		if (borderCursorRegistrationFailureLogged) return false;
	}

	RAWINPUTDEVICE rawInputDevice{};
	rawInputDevice.usUsagePage = 0x01; // Generic Desktop Controls
	rawInputDevice.usUsage = 0x02; // Mouse
	rawInputDevice.dwFlags = RIDEV_INPUTSINK;
	rawInputDevice.hwndTarget = hWnd;
	if (!RegisterRawInputDevices(&rawInputDevice, 1, sizeof(rawInputDevice)))
	{
		DWORD registrationError = GetLastError();
		bool needLog = false;
		{
			lock_guard lock(borderCursorLightMutex);
			borderCursorInputAvailable = false;
			borderCursorRawInputRegistered = false;
			if (!borderCursorRegistrationFailureLogged)
			{
				borderCursorRegistrationFailureLogged = true;
				needLog = true;
			}
		}
		if (needLog && IDTLogger) IDTLogger->error(
			"[BarUISetClass::SetBorderCursorRawInputEnabled] 注册全局鼠标 Raw Input 失败, error={}",
			registrationError);
		return false;
	}

	{
		lock_guard lock(borderCursorLightMutex);
		borderCursorInputAvailable = true;
		borderCursorRawInputRegistered = true;
	}
	return true;
}

void BarUISetClass::ActivateBorderCursorTracking(HWND hWnd)
{
	if (!hWnd || !BarUiAnimationEnabled
		|| !BarUiEdgeLightingEnabled || !BarUiDynamicEdgeLightingEnabled) return;
	{
		lock_guard lock(borderCursorLightMutex);
		if (borderCursorActivationBlockedUntilLeave) return;
	}

	POINT screenPoint{};
	bool cursorReady = GetCursorPos(&screenPoint);
	if (!cursorReady) return;

	bool cancelGraceTimer = false;
	bool needRegistration = false;
	bool inputStateChanged = false;
	{
		lock_guard lock(borderCursorLightMutex);
		cancelGraceTimer =
			borderCursorTrackingState == BarBorderCursorTrackingStateEnum::Grace;
		inputStateChanged = !borderCursorInputAvailable;
		borderCursorTrackingState = BarBorderCursorTrackingStateEnum::Inside;
		borderCursorGraceDeadlineTick = 0;
		needRegistration = !borderCursorRawInputRegistered;
	}
	if (cancelGraceTimer) KillTimer(hWnd, BarBorderCursorGraceTimerId);
	if (needRegistration && !SetBorderCursorRawInputEnabled(hWnd, true)) return;

	// 接受区只控制生命周期；240px 邻近判断仅用于裁剪无效渲染唤醒。
	bool cursorNearVisibleRegion = IsBorderCursorLightNearVisibleRegion(screenPoint);
	POINT clientPoint = screenPoint;
	if (!ScreenToClient(hWnd, &clientPoint)) return;
	bool needRendering = false;
	{
		lock_guard lock(borderCursorLightMutex);
		if (!borderCursorRawInputRegistered) return;
		bool proximityChanged =
			borderCursorLightNearVisibleRegion != cursorNearVisibleRegion;
		borderCursorLightNearVisibleRegion = cursorNearVisibleRegion;
		D2D1_POINT_2F nextPoint = D2D1::Point2F(
			static_cast<FLOAT>(clientPoint.x), static_cast<FLOAT>(clientPoint.y));
		// Inside 始终发布真实光标点，不能依赖首帧可见区域缓存已经完成。
		bool cursorChanged = !borderCursorLightReady
			|| nextPoint.x != borderCursorLightPoint.x || nextPoint.y != borderCursorLightPoint.y;
		borderCursorLightReady = true;
		if (cursorChanged)
		{
			borderCursorLightPoint = nextPoint;
			++borderCursorLightSerial;
		}
		else if (proximityChanged) ++borderCursorLightSerial;
		needRendering = inputStateChanged || cursorChanged || proximityChanged;
	}
	if (needRendering) UpdateRendering(false);
}

void BarUISetClass::RegisterBorderCursorLight(HWND hWnd)
{
	if (!hWnd || !BarUiEdgeLightingEnabled || !BarUiDynamicEdgeLightingEnabled) return;
	{
		lock_guard lock(borderCursorLightMutex);
		if (!borderCursorRawInputRegistered
			|| borderCursorTrackingState == BarBorderCursorTrackingStateEnum::Dormant)
			return;
	}

	POINT screenPoint{};
	if (!GetCursorPos(&screenPoint)) return;
	if (WindowFromPoint(screenPoint) == hWnd)
	{
		ActivateBorderCursorTracking(hWnd);
		return;
	}

	ULONGLONG now = GetTickCount64();
	bool startGraceTimer = false;
	bool graceExpired = false;
	{
		lock_guard lock(borderCursorLightMutex);
		if (!borderCursorRawInputRegistered) return;
		if (borderCursorTrackingState == BarBorderCursorTrackingStateEnum::Inside)
		{
			// 截止时间只在首次离开时确定，区域外连续移动不能延长宽限期。
			borderCursorTrackingState = BarBorderCursorTrackingStateEnum::Grace;
			borderCursorGraceDeadlineTick = now + BarBorderCursorGraceDurationMs;
			startGraceTimer = true;
		}
		else if (borderCursorTrackingState == BarBorderCursorTrackingStateEnum::Grace)
			graceExpired = now >= borderCursorGraceDeadlineTick;
	}
	if (graceExpired)
	{
		SuspendBorderCursorTracking(hWnd);
		return;
	}
	if (startGraceTimer && !ScheduleBorderCursorGraceTimer(
		hWnd, static_cast<UINT>(BarBorderCursorGraceDurationMs)))
	{
		SuspendBorderCursorTracking(hWnd);
		return;
	}

	// 区域外仍更新同一光源点；邻近判断不参与任何亮度公式。
	bool cursorNearVisibleRegion = IsBorderCursorLightNearVisibleRegion(screenPoint);
	POINT clientPoint = screenPoint;
	if (!ScreenToClient(hWnd, &clientPoint)) return;
	bool needRendering = false;
	{
		lock_guard lock(borderCursorLightMutex);
		if (!borderCursorRawInputRegistered
			|| borderCursorTrackingState != BarBorderCursorTrackingStateEnum::Grace)
			return;

		bool proximityChanged =
			borderCursorLightNearVisibleRegion != cursorNearVisibleRegion;
		borderCursorLightNearVisibleRegion = cursorNearVisibleRegion;
		bool cursorChanged = false;
		if (cursorNearVisibleRegion || proximityChanged)
		{
			D2D1_POINT_2F nextPoint = D2D1::Point2F(
				static_cast<FLOAT>(clientPoint.x), static_cast<FLOAT>(clientPoint.y));
			// 跨出 240px 时发布最后一个位置，让 240px 径向渐变自然落到 0。
			cursorChanged = !borderCursorLightReady
				|| nextPoint.x != borderCursorLightPoint.x || nextPoint.y != borderCursorLightPoint.y;
			if (cursorChanged)
			{
				borderCursorLightPoint = nextPoint;
				borderCursorLightReady = true;
				++borderCursorLightSerial;
			}
		}
		else if (proximityChanged) ++borderCursorLightSerial;
		needRendering = proximityChanged || cursorChanged;
	}
	if (needRendering && BarUiAnimationEnabled) UpdateRendering(false);
}

void BarUISetClass::HandleBorderCursorGraceTimeout(HWND hWnd)
{
	ULONGLONG now = GetTickCount64();
	ULONGLONG remaining = 0;
	bool shouldSuspend = false;
	{
		lock_guard lock(borderCursorLightMutex);
		if (borderCursorTrackingState != BarBorderCursorTrackingStateEnum::Grace)
		{
			KillTimer(hWnd, BarBorderCursorGraceTimerId);
			return;
		}
		if (now >= borderCursorGraceDeadlineTick) shouldSuspend = true;
		else remaining = borderCursorGraceDeadlineTick - now;
	}
	if (shouldSuspend)
	{
		SuspendBorderCursorTracking(hWnd);
		return;
	}
	if (!ScheduleBorderCursorGraceTimer(
		hWnd, static_cast<UINT>(max<ULONGLONG>(1, remaining))))
		SuspendBorderCursorTracking(hWnd);
}

void BarUISetClass::HandleCanvasDrawingActivity(HWND hWnd, bool started)
{
	if (!hWnd || !started) return;
	if (started && BarCanvasDrawingActivityCount.load(std::memory_order_acquire) == 0)
		return;

	POINT screenPoint{};
	if (!GetCursorPos(&screenPoint) || WindowFromPoint(screenPoint) == hWnd) return;

	// 落笔只在消息接收区外一次性关闭第三鼠标光，第一光源与后续绘制过程保持独立。
	SuspendBorderCursorTracking(hWnd);
}

void BarUISetClass::SuspendBorderCursorTracking(HWND hWnd, bool waitForMouseLeave)
{
	if (hWnd) KillTimer(hWnd, BarBorderCursorGraceTimerId);
	bool blockActivationUntilLeave = false;
	if (waitForMouseLeave && hWnd)
	{
		POINT screenPoint{};
		if (GetCursorPos(&screenPoint) && WindowFromPoint(screenPoint) == hWnd)
		{
			TRACKMOUSEEVENT trackMouseEvent{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
			blockActivationUntilLeave = TrackMouseEvent(&trackMouseEvent) != FALSE;
		}
	}
	bool needRendering = false;
	{
		lock_guard lock(borderCursorLightMutex);
		needRendering = borderCursorInputAvailable
			|| borderCursorRawInputRegistered
			|| borderCursorLightNearVisibleRegion;
		borderCursorTrackingState = BarBorderCursorTrackingStateEnum::Dormant;
		borderCursorGraceDeadlineTick = 0;
		borderCursorActivationBlockedUntilLeave = blockActivationUntilLeave;
	}
	SetBorderCursorRawInputEnabled(hWnd, false);
	if (needRendering) UpdateRendering(false);
}

bool BarUISetClass::ScheduleBorderCursorGraceTimer(HWND hWnd, UINT delayMs)
{
	if (hWnd && SetTimer(hWnd, BarBorderCursorGraceTimerId, max<UINT>(1, delayMs), nullptr))
		return true;

	DWORD timerError = GetLastError();
	bool needLog = false;
	{
		lock_guard lock(borderCursorLightMutex);
		if (!borderCursorTimerFailureLogged)
		{
			borderCursorTimerFailureLogged = true;
			needLog = true;
		}
	}
	if (needLog && IDTLogger) IDTLogger->error(
		"[BarUISetClass::ScheduleBorderCursorGraceTimer] 创建第三光源休眠定时器失败, error={}",
		timerError);
	return false;
}

void BarUISetClass::RefreshBorderCursorVisibleRegions()
{
	array<RECT, 3> nextRegions{};
	size_t nextCount = 0;
	double zoom = barStyle.zoom;
	auto AddShape = [&](const shared_ptr<BarUiShapeClass>& shape)
		{
			if (!shape || !shape->enable.val || shape->pct.val <= 0.000001
				|| nextCount >= nextRegions.size())
				return;
			nextRegions[nextCount++] = BarRenderingAttribute::GetWeigetRect(*shape, zoom);
		};
	auto AddSuperellipse = [&](const shared_ptr<BarUiSuperellipseClass>& superellipse)
		{
			if (!superellipse || !superellipse->enable.val
				|| superellipse->pct.val <= 0.000001 || nextCount >= nextRegions.size())
				return;
			nextRegions[nextCount++] =
				BarRenderingAttribute::GetWeigetRect(*superellipse, zoom);
		};

	AddSuperellipse(superellipseMap[BarUISetSuperellipseEnum::MainButton]);
	AddShape(shapeMap[BarUISetShapeEnum::MainBar]);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar]);

	// 距离判断统一在屏幕坐标完成，避免接受区内外分别换算客户区坐标。
	POINT clientOrigin{};
	if (!floating_window || !ClientToScreen(floating_window, &clientOrigin))
	{
		nextCount = 0;
	}
	for (size_t i = 0; i < nextCount; ++i)
	{
		nextRegions[i].left += clientOrigin.x;
		nextRegions[i].right += clientOrigin.x;
		nextRegions[i].top += clientOrigin.y;
		nextRegions[i].bottom += clientOrigin.y;
	}

	lock_guard lock(borderCursorLightMutex);
	borderCursorVisibleRegions = nextRegions;
	borderCursorVisibleRegionCount = nextCount;
}

bool BarUISetClass::IsBorderCursorLightNearVisibleRegion(POINT screenPoint)
{
	array<RECT, 3> visibleRegions{};
	size_t visibleRegionCount = 0;
	{
		lock_guard lock(borderCursorLightMutex);
		visibleRegions = borderCursorVisibleRegions;
		visibleRegionCount = borderCursorVisibleRegionCount;
	}

	double zoom = barStyle.zoom;
	if (!isfinite(zoom) || zoom <= 0.0) return false;
	double distanceLimit = BarBorderCursorLightRadius * zoom;
	if (distanceLimit <= 0.0) return false;
	double distanceLimitSquared = distanceLimit * distanceLimit;
	for (size_t i = 0; i < visibleRegionCount; i++)
	{
		const RECT& region = visibleRegions[i];
		double dx = 0.0;
		double dy = 0.0;
		if (screenPoint.x < region.left) dx = static_cast<double>(region.left - screenPoint.x);
		else if (screenPoint.x > region.right) dx = static_cast<double>(screenPoint.x - region.right);
		if (screenPoint.y < region.top) dy = static_cast<double>(region.top - screenPoint.y);
		else if (screenPoint.y > region.bottom) dy = static_cast<double>(screenPoint.y - region.bottom);
		double distanceSquared = dx * dx + dy * dy;
		if (distanceSquared <= distanceLimitSquared) return true;
	}
	return false;
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
	void SetAnimationOptions(bool enable, double speedRate)
	{
		speedRate = isfinite(speedRate) ? clamp(speedRate, 0.1, 5.0) : 1.0;
		// 使用足够大的有限倍率统一完成普通动画、批次和 SVG 关键帧，不能用 0 让时间轴停住。
		BarUiAnimationEnabled = enable;
		BarUiAnimationSpeedRate = enable ? speedRate : 1.0e12;
		if (!enable && floating_window)
			PostMessage(floating_window, BarBorderCursorSuspendMessage, 0, 0);
		barUISet.UpdateRendering(false);
	}

	void SetEdgeLightingOptions(bool enable, bool dynamic)
	{
		BarUiEdgeLightingEnabled = enable;
		BarUiDynamicEdgeLightingEnabled = dynamic;
		// 关闭任一级动态光门禁时，统一交给 Bar 窗口线程注销 Raw Input。
		if ((!enable || !dynamic) && floating_window)
			PostMessage(floating_window, BarBorderCursorSuspendMessage, 0, 0);
		barUISet.UpdateRendering(false);
	}

	void SetDebugMode(bool enable)
	{
		BarUiDebugModeEnabled = enable;
		// 关闭时也请求一帧，用新旧脏区并集清除 FPS 文本与红框。
		barUISet.UpdateRendering(false);
	}

	void NotifyCanvasDrawingStarted()
	{
		// 只在首个并发笔迹进入时通知窗口线程，避免每个采样或多指笔迹重复切换状态。
		if (BarCanvasDrawingActivityCount.fetch_add(1, std::memory_order_acq_rel) == 0
			&& floating_window)
			PostMessage(floating_window, BarCanvasDrawingActivityMessage, 1, 0);
	}

	void NotifyCanvasDrawingEnded()
	{
		unsigned int activityCount =
			BarCanvasDrawingActivityCount.load(std::memory_order_acquire);
		while (activityCount != 0
			&& !BarCanvasDrawingActivityCount.compare_exchange_weak(
				activityCount, activityCount - 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
		{
		}
	}

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
		SetThemeStyleSource(&barUISet.barStyle);

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
				auto word = make_shared<BarUiWordClass>(700.0, 150.0, 1200.0, 300.0, L"", 30.0, GetThemeColor(BarThemeColorEnum::TextPrimary));
				word->content.Initialization(L"软件遇到透明背景无法正常显示的故障\n\nexe属性->关闭使用简化的颜色模式\nWindows7用户请开启Aero主题\n\n联系开发者->软件选项主页中\n重启软件试试");
				word->pct.Initialization(0.0);
				word->enable.Initialization(true);
				barUISet.wordMap[BarUISetWordEnum::BackgroundWarning] = word;
			}

			// 主按钮
			{
				auto superellipse = make_shared<BarUiSuperellipseClass>(mainX, mainY, 80.0, 80.0, 3.0, 1.0, GetThemeColor(BarThemeColorEnum::Surface), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				superellipse->pct.Initialization(0.6);
				superellipse->framePct = BarUiPctClass(0.18);
				superellipse->frameRendering = BarUiFrameRenderingEnum::PointLight;
				superellipse->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
				superellipse->enable.Initialization(true);
				barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton] = superellipse;

				{
					auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
					svg->InitializationFromResource(L"UI", barUISet.barStyle.darkStyle ? L"logo1" : L"logo2");
					svg->SetWH(nullopt, 80.0);
					svg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::logo1] = svg;
				}
				{
					auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, GetPenColor(), nullopt);
					svg->InitializationFromResource(L"UI", L"Frame94");
					svg->SetWH(nullopt, 80.0);
					svg->pct.Initialization(0.0); // 首帧先隐藏，避免状态计算前闪烁错误颜色。
					svg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::logoInk] = svg;
				}
				{
					// TODO “收起” 文字标识
				}
			}
			// 主栏
			{
				auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 80.0, 80.0, 8.0, 8.0, 1.0, GetThemeColor(BarThemeColorEnum::Surface), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				shape->pct.Initialization(0.8);
				shape->framePct = BarUiPctClass(0.18);
				shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
				shape->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
				shape->w.mod = BarUiValueModeEnum::Variable;
				shape->h.mod = BarUiValueModeEnum::Variable;
				shape->enable.Initialization(true);
				barUISet.shapeMap[BarUISetShapeEnum::MainBar] = shape;

				// 绘制属性（一级菜单）
				{
					// 初值就是绘制按钮中心处的等比紧凑态，避免首轮展开从旧 60×60 几何起步。
					auto shape = make_shared<BarUiShapeClass>(
						0.0, 0.0,
						BarDrawAttributeCompactWidth, BarDrawAttributeCompactHeight,
						8.0 * BarDrawAttributeCompactScale,
						8.0 * BarDrawAttributeCompactScale,
						BarDrawAttributeCompactScale,
						GetThemeColor(BarThemeColorEnum::Surface),
						GetThemeColor(BarThemeColorEnum::SurfaceFrame));
					shape->pct.Initialization(
						BarDrawAttributeSurfaceOpacity);
					shape->framePct = BarUiPctClass(0.18);
					shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
					shape->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
					shape->w.mod = BarUiValueModeEnum::Variable;
					shape->h.mod = BarUiValueModeEnum::Variable;
					shape->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar] = shape;

					// Color 区域
					{
						// Color 1
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect1), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect2), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect3), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect4), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect5), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect6), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect7), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect8), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect9), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect10), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
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
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect11), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11] = svg;
						}
						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
						{
							// 颜色块沿用普通边框色与点光参数，但保持 Frame 策略，禁止画笔染色。
							auto shape = barUISet.shapeMap[static_cast<BarUISetShapeEnum>(i)];
							shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
							shape->framePct = BarUiPctClass(0.0);
							shape->framePrimaryLightEnabled = false;
							shape->frameCursorLightIntensityScale = BarColorSwatchCursorLightIntensity;
							// 18% 只控制基础灰边；色块仅由鼠标追光随填充进度完整显现。
							shape->frameLightOpacitySource = BarUiFrameLightOpacitySourceEnum::ObjectPct;
						}
					}
					{ /**/ }
					// 画笔样式区域
					{
						auto InitializePenTypeButton = [&](BarUISetShapeEnum shapeType,
							BarUISetSvgEnum svgType, BarUISetWordEnum wordType,
							const wchar_t* resourceName, const wchar_t* text)
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 115.0, 30.0,
								4.0, 4.0, 1.0, GetThemeColor(BarThemeColorEnum::PressedFill), nullopt);
							shape->enable.Initialization(true);
							barUISet.shapeMap[shapeType] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
							svg->InitializationFromResource(L"UI", resourceName);
							svg->SetWH(18.0, 18.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[svgType] = svg;

							auto word = make_shared<BarUiWordClass>(0.0, 0.0, 80.0, 30.0,
								text, 12.0, GetThemeColor(BarThemeColorEnum::TextPrimary));
							word->enable.Initialization(true);
							barUISet.wordMap[wordType] = word;
						};
						// 从上到下固定为刷子、激光笔、荧光笔、硬笔、软笔。
						InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Brush2,
							BarUISetSvgEnum::DrawAttributeBar_Brush2,
							BarUISetWordEnum::DrawAttributeBar_Brush2,
							L"barBrush1", L"刷子");
						InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Laser,
							BarUISetSvgEnum::DrawAttributeBar_Laser,
							BarUISetWordEnum::DrawAttributeBar_Laser,
							L"barBrush1", L"激光笔");
						InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Highlight1,
							BarUISetSvgEnum::DrawAttributeBar_Highlight1,
							BarUISetWordEnum::DrawAttributeBar_Highlight1,
							L"barHighlighter1", L"荧光笔");
						InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Brush1,
							BarUISetSvgEnum::DrawAttributeBar_Brush1,
							BarUISetWordEnum::DrawAttributeBar_Brush1,
							L"barBrush1", L"硬笔");
						// 临时统一复用硬笔图标，待三种笔型图标完善后再恢复各自资源。
						InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_SoftPen,
							BarUISetSvgEnum::DrawAttributeBar_SoftPen,
							BarUISetWordEnum::DrawAttributeBar_SoftPen,
							L"barBrush1", L"软笔");
					}
					// 粗细调节区域
					{
						auto shape = make_shared<BarUiShapeClass>(0.0, 0.0,
							240.0, BarDrawAttributeThicknessHeight,
							4.0, 4.0, 1.0,
							nullopt, GetThemeColor(BarThemeColorEnum::SurfaceFrame));
						shape->pct.Initialization(0.0);
						shape->framePct = BarUiPctClass(0.0);
						shape->frameLightPct = BarUiPctClass(0.0);
						// 粗细外框保持透明，仅用基础灰边承载第三鼠标光。
						shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
						shape->frameLightColor = BarUiFrameLightColorEnum::Frame;
						shape->framePrimaryLightEnabled = false;
						shape->frameCursorLightIntensityScale =
							BarButtonCursorLightIntensity;
						shape->enable.Initialization(true);
						barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect] = shape;

						auto word = make_shared<BarUiWordClass>(0.0, 0.0,
							90.0, 30.0, L"", 13.0,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						word->enable.Initialization(true);
						barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay] = word;

						auto InitializeThicknessButton = [&](BarUISetShapeEnum shapeType)
							{
								auto button = make_shared<BarUiShapeClass>(
									0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0,
									GetThemeColor(BarThemeColorEnum::PressedFill),
									GetThemeColor(BarThemeColorEnum::TextPrimary));
								button->pct.Initialization(0.0);
								button->framePct = BarUiPctClass(0.0);
								button->frameLightPct = BarUiPctClass(0.0);
								button->frameRendering =
									BarUiFrameRenderingEnum::PointLight;
								button->framePrimaryLightEnabled = false;
								button->frameCursorLightIntensityScale =
									BarButtonCursorLightIntensity;
								button->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = button;
							};
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessFine);
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium);
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse);
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust);
						auto adjustSvg = make_shared<BarUiSVGClass>(
							0.0, 0.0,
							GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
						adjustSvg->InitializationFromResource(
							L"UI", L"barThicknessAdjust");
						adjustSvg->SetWH(18.0, 18.0);
						adjustSvg->pct.Initialization(0.0);
						adjustSvg->enable.Initialization(true);
						barUISet.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust] =
							adjustSvg;

						const BarUISetWordEnum numberWords[] =
						{
							BarUISetWordEnum::DrawAttributeBar_ThicknessFineNumber,
							BarUISetWordEnum::DrawAttributeBar_ThicknessMediumNumber,
							BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber,
						};
						for (auto wordType : numberWords)
						{
							auto numberWord = make_shared<BarUiWordClass>(
								0.0, 0.0, 30.0, 30.0, L"", 10.0,
								GetThemeColor(BarThemeColorEnum::TextPrimary));
							numberWord->pct.Initialization(0.0);
							numberWord->enable.Initialization(true);
							barUISet.wordMap[wordType] = numberWord;
						}

						auto InitializeTooltipSurface =
							[&](BarUISetShapeEnum shapeType, double width, double height)
							{
								auto surface = make_shared<BarUiShapeClass>(
									0.0, 0.0, width, height, 4.0, 4.0, 1.0,
									GetThemeColor(BarThemeColorEnum::Surface),
									GetThemeColor(BarThemeColorEnum::SurfaceFrame));
								surface->pct.Initialization(0.0);
								surface->framePct = BarUiPctClass(0.0);
								surface->frameLightPct = BarUiPctClass(0.0);
								surface->frameRendering =
									BarUiFrameRenderingEnum::PointLight;
								surface->frameLightColor =
									BarUiFrameLightColorEnum::Frame;
								surface->framePrimaryLightEnabled = false;
								surface->frameCursorLightIntensityScale =
									BarButtonCursorLightIntensity;
								surface->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = surface;
							};
						auto InitializeTooltipHit =
							[&](BarUISetShapeEnum shapeType, double size)
							{
								auto hit = make_shared<BarUiShapeClass>(
									0.0, 0.0, size, size, nullopt, nullopt,
									nullopt, nullopt, nullopt);
								hit->pct.Initialization(0.0);
								hit->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = hit;
							};
						auto InitializeTooltipCloseButton =
							[&](BarUISetShapeEnum shapeType)
							{
								auto button = make_shared<BarUiShapeClass>(
									0.0, 0.0,
									BarThicknessTooltipCloseButtonSize,
									BarThicknessTooltipCloseButtonSize,
									4.0, 4.0, nullopt,
									GetThemeColor(
										BarThemeColorEnum::PressedFill),
									nullopt);
								button->pct.Initialization(0.0);
								button->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = button;
							};

						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationBadge,
							72.0, 24.0);
						InitializeTooltipHit(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit,
							14.0);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge,
							24.0, 24.0);
						InitializeTooltipHit(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit,
							14.0);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
							1.0, 1.0);
						InitializeTooltipCloseButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
							1.0, 1.0);
						InitializeTooltipCloseButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit);

						auto annotationLabel = make_shared<BarUiWordClass>(
							0.0, 0.0, 48.0, 24.0, L"标注线", 13.0,
							RGB(200, 200, 200));
						annotationLabel->pct.Initialization(0.0);
						annotationLabel->enable.Initialization(true);
						barUISet.wordMap[
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel] =
							annotationLabel;

						COLORREF popupBodyColor = MixBarUiColor(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							GetThemeColor(BarThemeColorEnum::Surface), 0.45);
						auto InitializeTooltipWord =
							[&](BarUISetWordEnum wordType, const wchar_t* text,
								double size, COLORREF color)
							{
								auto word = make_shared<BarUiWordClass>(
									0.0, 0.0, 1.0, 1.0, text, size, color);
								word->pct.Initialization(0.0);
								word->enable.Initialization(true);
								barUISet.wordMap[wordType] = word;
							};
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
							L"启用标注线（暂不可用）",
							BarThicknessTooltipTitleFontSize,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
							L"锁定绘制方向仅为水平、竖直或斜45°",
							BarThicknessTooltipBodyFontSize, popupBodyColor);
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
							L"墨迹粗细超出预览范围",
							BarThicknessTooltipTitleFontSize,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
							L"预览中的粗细可能与绘制粗细不一致。",
							BarThicknessTooltipBodyFontSize, popupBodyColor);

						auto InitializeTooltipSvg =
							[&](BarUISetSvgEnum svgType, const wchar_t* resourceName,
								COLORREF color, double size)
							{
								auto svg = make_shared<BarUiSVGClass>(
									0.0, 0.0, color, nullopt);
								svg->InitializationFromResource(L"UI", resourceName);
								svg->SetWH(size, size);
								svg->pct.Initialization(0.0);
								svg->enable.Initialization(true);
								barUISet.svgMap[svgType] = svg;
							};
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo,
							L"barQuestion", RGB(200, 200, 200), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo,
							L"barInfo", RGB(255, 255, 255), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
							L"barCloseSmall",
							GetThemeColor(BarThemeColorEnum::TextPrimary), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
							L"barCloseSmall",
							GetThemeColor(BarThemeColorEnum::TextPrimary), 14.0);
					}
				}
			}
		}
	}
}
