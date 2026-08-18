module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../Business/LegacyDrawState.hpp"
#include "../../../IdtState.h"
#include <d2d1helper.h>
#include "../../Window/Window.Legacy.hpp"
#include "Bar.BottomDock.h"
#include "Bar.WindowGeometry.h"

module Inkeys.UI.Bar;
import :Main;
import :Layout;
import :Atomic;
import :Theme;

import Inkeys.Conv.Color;
import Inkeys.Message;
import Inkeys.Other.Inputs;
import Inkeys.Window;
import Inkeys.Display;
using Inkeys::UI::Bar::BarToggleChannel;
constexpr double BarButtonHoverOpacity = 0.18;
constexpr double BarButtonHoverShowDur = 0.24;
constexpr double BarButtonHoverExitDur = 0.24;
constexpr ULONGLONG BarBorderCursorGraceDurationMs = 5000;
constexpr UINT_PTR BarBorderCursorGraceTimerId = 0x494B4301;
constexpr UINT BarThicknessTooltipHoverGraceMs = 100;
constexpr UINT_PTR BarThicknessAnnotationTooltipGraceTimerId = 0x494B4302;
constexpr UINT_PTR BarThicknessOverflowTooltipGraceTimerId = 0x494B4303;
constexpr UINT BarBorderCursorSuspendMessage = WM_APP + 0x31;
constexpr UINT BarCanvasDrawingActivityMessage = WM_APP + 0x32;
constexpr UINT BarThicknessSliderCaptureMessage = WM_APP + 0x33;
constexpr UINT BarColorPickerCaptureMessage = WM_APP + 0x34;
constexpr short BarTouchPointerMessageMarker = SHRT_MIN;
constexpr short BarTouchCancelMessageMarker = SHRT_MIN + 1;
constexpr short BarTouchDirectDragScreenMessageMarker = SHRT_MIN + 2;
constexpr short BarTouchCancelScreenMessageMarker = SHRT_MIN + 3;
constexpr WPARAM BarThicknessSliderCaptureStop = 0;
constexpr WPARAM BarThicknessSliderCaptureStart = 1;
constexpr WPARAM BarThicknessSliderCaptureCancel = 2;
constexpr WPARAM BarColorPickerCaptureStop = 0;
constexpr WPARAM BarColorPickerCaptureStart = 1;
constexpr WPARAM BarColorPickerCaptureCancel = 2;
constexpr double BarThicknessPreviewTouchSlopDip = 5.0;
constexpr ULONGLONG BarThicknessFineDialActivationDwellMs = 1000;
constexpr auto BarThicknessFineDialPhysicsPollInterval =
	chrono::milliseconds(8);
constexpr double BarThicknessFineDialMaxDtSeconds = 0.032;
constexpr size_t BarThicknessFineDialVelocitySampleCount = 6;
constexpr ULONGLONG BarThicknessFineDialVelocityWindowMs = 96;
constexpr double BarThicknessFineDialReleaseVelocityDipPerSecond = 80.0;
constexpr double BarThicknessFineDialMaxVelocityDipPerSecond = 900.0;
constexpr double BarThicknessFineDialFrictionPerSecond = 10.0;
constexpr double BarThicknessFineDialResidualWeight = 0.35;
constexpr double BarThicknessFineDialResidualDecayPerSecond = 6.0;
constexpr double BarThicknessFineDialRubberBandLimitDip = 24.0;
constexpr double BarThicknessFineDialSpringOmega = 18.0;
constexpr double BarThicknessFineDialSpringDampingRatio = 1.05;
constexpr double BarThicknessFineDialSettleDistanceDip = 0.15;
constexpr double BarThicknessFineDialSettleVelocityDipPerSecond = 4.0;
// 拖动静止后先显示提示，再到达锁定时限。
constexpr double BarThicknessHoldStillnessPx = 5.0;
constexpr ULONGLONG BarThicknessHoldHintDelayMs = 500;
constexpr ULONGLONG BarThicknessHoldLockDelayMs = 1500;
constexpr double BarColorPickerKeyboardStepDip = 2.0;
constexpr double BarColorPickerHoldStillnessPx = 5.0;
constexpr ULONGLONG BarColorPickerHoldHintDelayMs = 500;
constexpr ULONGLONG BarColorPickerHoldLockDelayMs = 1500;

std::atomic_uint BarCanvasDrawingActivityCount = 0;
// 消息线程拥有入口按压态，渲染线程只通过只读接口取得缩放目标。
IdtAtomic<bool> BarColorPickerEntryPressed = false;

bool ReadColorPickerEntryPressed()
{
	return BarColorPickerEntryPressed;
}

void RequestBarBorderCursorSuspend()
{
	if (floating_window)
		PostMessage(floating_window, BarBorderCursorSuspendMessage, 0, 0);
}
enum class BarThicknessFineDialHitZone : int
{
	None,
	Consumed,
	Drag,
};

bool IsBarThicknessPreviewPopupHit(
	BarUISetClass& barUISet, int clientX, int clientY)
{
	auto viewMode = barUISet.barState.drawAttributeBar.thicknessViewMode;
	if (viewMode != ThicknessViewMode::Slider
		&& viewMode != ThicknessViewMode::FineDial)
		return false;
	auto popupSurface = barUISet.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessPreviewPopupSurface];
	return popupSurface && popupSurface->pct.val > 0.000001
		&& popupSurface->w.val > 0.0 && popupSurface->h.val > 0.0
		&& popupSurface->IsClick(
			clientX, clientY, barUISet.barStyle.zoom);
}

bool TryGetBarThicknessFineDialActivationGeometry(
	BarUISetClass& barUISet, BarThicknessFineDialGeometry& geometry)
{
	auto& drawAttribute = barUISet.barState.drawAttributeBar;
	if (drawAttribute.thicknessViewMode != ThicknessViewMode::Slider)
		return false;
	auto panel = barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
	auto region = barUISet.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
	auto adjust = barUISet.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
	auto sliderThumb = barUISet.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderThumb];
	if (!panel || !region || !adjust)
		return false;

	auto previewGeometry = CalculateBarThicknessPreviewGeometry(
		*panel, *region, BarUiInheritClass(region->inhX, region->inhY),
		*adjust, BarUiInheritClass(adjust->inhX, adjust->inhY));
	double sliderCenterY = previewGeometry.sliderCenterY;
	if (sliderThumb && sliderThumb->w.val > 0.0
		&& sliderThumb->h.val > 0.0)
		sliderCenterY = sliderThumb->inhY + sliderThumb->h.val / 2.0;
	geometry = CalculateBarThicknessFineDialGeometry(
		previewGeometry, sliderCenterY, panel->inhY,
		panel->inhY + panel->h.val);
	return geometry.valid;
}

BarThicknessFineDialHitZone HitTestBarThicknessFineDialFreshActivation(
	BarUISetClass& barUISet, int clientX, int clientY)
{
	BarThicknessFineDialGeometry geometry;
	if (!TryGetBarThicknessFineDialActivationGeometry(barUISet, geometry))
		return BarThicknessFineDialHitZone::None;
	// Popup 拥有独立点击/拖动语义，优先于下方 FineDial corridor。
	if (IsBarThicknessPreviewPopupHit(barUISet, clientX, clientY))
		return BarThicknessFineDialHitZone::None;
	double zoom = static_cast<double>(barUISet.barStyle.zoom);
	if (!IsBarClientPointInLogicalRect(
		clientX, clientY, zoom, geometry.ownershipCorridor))
		return BarThicknessFineDialHitZone::None;

	// 动画未完成时 corridor 仍归 FineDial 专用，但暂不允许激活。
	auto sliderThumb = barUISet.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderThumb];
	if (!sliderThumb || sliderThumb->w.val <= 0.0
		|| sliderThumb->h.val <= 0.0
		|| static_cast<double>(sliderThumb->pct.val) < 0.999999)
		return BarThicknessFineDialHitZone::Consumed;

	if (IsBarClientPointInLogicalRect(
		clientX, clientY, zoom, geometry.dragZone))
		return BarThicknessFineDialHitZone::Drag;
	if (IsBarClientPointInLogicalRect(
		clientX, clientY, zoom, geometry.clickZone))
		return BarThicknessFineDialHitZone::Consumed;
	return BarThicknessFineDialHitZone::Consumed;
}

bool IsBarThicknessFineDialDwellZone(
	BarUISetClass& barUISet, int clientX, int clientY)
{
	BarThicknessFineDialGeometry geometry;
	if (!TryGetBarThicknessFineDialActivationGeometry(barUISet, geometry))
		return false;
	return IsBarClientPointInLogicalRect(
		clientX, clientY, static_cast<double>(barUISet.barStyle.zoom),
		geometry.clickZone);
}

bool IsBarThicknessPrecisionDragHit(
	BarUISetClass& barUISet, int mx, int my)
{
	return HitTestBarThicknessFineDialFreshActivation(barUISet, mx, my)
		== BarThicknessFineDialHitZone::Drag;
}

void MarkBarTouchPointerMessage(ExMessage& message, bool cancelled = false,
	bool directDragScreenSample = false)
{
	// 非滚轮鼠标消息不使用 wheel，保留触摸转单指的来源标记。
	message.wheel = cancelled
		? (directDragScreenSample
			? BarTouchCancelScreenMessageMarker
			: BarTouchCancelMessageMarker)
		: (directDragScreenSample
			? BarTouchDirectDragScreenMessageMarker
			: BarTouchPointerMessageMarker);
}

bool IsBarTouchPointerMessage(const ExMessage& message)
{
	return message.message != WM_MOUSEWHEEL
		&& (message.wheel == BarTouchPointerMessageMarker
			|| message.wheel == BarTouchCancelMessageMarker
			|| message.wheel == BarTouchDirectDragScreenMessageMarker
			|| message.wheel == BarTouchCancelScreenMessageMarker);
}

bool IsBarTouchCancelMessage(const ExMessage& message)
{
	return message.message != WM_MOUSEWHEEL
		&& (message.wheel == BarTouchCancelMessageMarker
			|| message.wheel == BarTouchCancelScreenMessageMarker);
}

bool IsBarTouchScreenMessage(const ExMessage& message)
{
	return message.message != WM_MOUSEWHEEL
		&& (message.wheel == BarTouchDirectDragScreenMessageMarker
			|| message.wheel == BarTouchCancelScreenMessageMarker);
}

struct BarTouchScreenSample
{
	POINT point{};
	bool ready = false;
};

bool IsBarCoordinateMessage(UINT message)
{
	switch (message)
	{
	case WM_MOUSEMOVE:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
		return true;
	default:
		return false;
	}
}

void ApplyBarBottomDockRigidHitTest(ExMessage& message)
{
	if (!IsBarCoordinateMessage(message.message)
		|| IsBarTouchScreenMessage(message))
		return;
	message.y = static_cast<short>(clamp(
		barUISet.BottomDockRigidHitTestY(message.y),
		static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
}

void ApplyBarBottomDockBodyHitTestFromRigid(
	ExMessage& message, int* visualY = nullptr)
{
	if (!IsBarCoordinateMessage(message.message)
		|| IsBarTouchScreenMessage(message))
	{
		if (visualY) *visualY = message.y;
		return;
	}
	message.y = static_cast<short>(clamp(
		barUISet.BottomDockBodyHitTestYFromRigid(
			message.y, visualY),
		static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
}

bool BarScreenToLayout(
	POINT& point, bool preserveScreenDuringDirectDrag = false)
{
	const auto presented = barUISet.BottomDockPresentedSnapshot();
	POINT presentedTranslation = presented.directTranslation;
	// WM_TOUCH 拖动采样保留物理屏幕坐标；其他命中和光源始终跟随实际 HWND。
	if (preserveScreenDuringDirectDrag)
		presentedTranslation =
			barUISet.DirectWindowPresentedTranslation(true);
	point = Inkeys::UI::Bar::BarScreenToLayoutPoint(
		point, presented.monitorOrigin,
		presentedTranslation);
	return true;
}

bool BarLayoutToScreen(POINT& point)
{
	const auto presented = barUISet.BottomDockPresentedSnapshot();
	point = Inkeys::UI::Bar::BarLayoutToScreenPoint(
		point, presented.monitorOrigin, presented.directTranslation);
	return true;
}

void PrepareBarInteractionMessage(ExMessage& message,
	bool preserveTouchScreenCoordinates,
	BarTouchScreenSample* touchScreenSample = nullptr)
{
	if (touchScreenSample) touchScreenSample->ready = false;
	if (IsBarTouchScreenMessage(message))
	{
		const POINT screenPoint{ message.x, message.y };
		if (touchScreenSample)
		{
			touchScreenSample->point = screenPoint;
			touchScreenSample->ready = true;
		}
		if (preserveTouchScreenCoordinates) return;

		const bool cancelled = IsBarTouchCancelMessage(message);
		POINT layoutPoint = screenPoint;
		BarScreenToLayout(layoutPoint);
		message.x = static_cast<short>(clamp<LONG>(
			layoutPoint.x, SHRT_MIN, SHRT_MAX));
		message.y = static_cast<short>(clamp<LONG>(
			layoutPoint.y, SHRT_MIN, SHRT_MAX));
		MarkBarTouchPointerMessage(message, cancelled, false);
	}
	ApplyBarBottomDockRigidHitTest(message);
}

bool WaitForBarInteractionMessage(ExMessage& message, BYTE filter, HWND hWnd,
	bool preserveTouchScreenCoordinates = false,
	BarTouchScreenSample* touchScreenSample = nullptr)
{
	// 保持原有轮询粒度，同时让退出时的 join 可终止。
	while (!offSignal)
	{
		if (Inkeys::Window::TryGet(hWnd, message,
			static_cast<Inkeys::Message::Filter>(filter)))
		{
			// 普通命中在消费时转换；拖动循环保留整次 contact 的绝对屏幕采样。
			PrepareBarInteractionMessage(message,
				preserveTouchScreenCoordinates, touchScreenSample);
			return true;
		}
		this_thread::sleep_for(chrono::milliseconds(1));
	}
	return false;
}

bool TryGetBarInteractionMessage(
	ExMessage* message, BYTE filter, bool removeMessage, HWND hWnd,
	BarTouchScreenSample* touchScreenSample = nullptr)
{
	// 旧交互路径全部是成功即消费，HiMsg 不提供 peek 语义。
	if (!message || !removeMessage || !Inkeys::Window::TryGet(
		hWnd, *message, static_cast<Inkeys::Message::Filter>(filter)))
		return false;
	PrepareBarInteractionMessage(*message, false, touchScreenSample);
	return true;
}

void ClearBarInteractionMessages(BYTE filter, HWND hWnd)
{
	(void)Inkeys::Window::Clear(
		hWnd, static_cast<Inkeys::Message::Filter>(filter));
}

void QueueBarThicknessSliderEnd(HWND hWnd)
{
	if (!hWnd) return;
	POINT point{};
	if (!GetCursorPos(&point)) point = {};
	BarScreenToLayout(point);

	ExMessage message{};
	message.message = WM_LBUTTONUP;
	message.x = static_cast<short>(clamp<LONG>(
		point.x, SHRT_MIN, SHRT_MAX));
	message.y = static_cast<short>(clamp<LONG>(
		point.y, SHRT_MIN, SHRT_MAX));
	message.lbutton = false;

	(void)Inkeys::Window::Enqueue(hWnd, message);
}

void QueueBarColorPickerEnd(HWND hWnd)
{
	if (!hWnd) return;
	POINT point{};
	if (!GetCursorPos(&point)) point = {};
	BarScreenToLayout(point);

	ExMessage message{};
	message.message = WM_LBUTTONUP;
	message.x = static_cast<short>(clamp<LONG>(point.x, SHRT_MIN, SHRT_MAX));
	message.y = static_cast<short>(clamp<LONG>(point.y, SHRT_MIN, SHRT_MAX));
	message.lbutton = false;

	(void)Inkeys::Window::Enqueue(hWnd, message);
}

// ====================
// 窗口

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DESTROY)
	{
		barUISet.ShutdownWindowInput(hWnd);
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
	// 关闭后不允许迟到的计时器或 Raw Input 重新建立交互/追踪状态。
	if (offSignal) return DefWindowProcW(hWnd, msg, wParam, lParam);
	if (msg == WM_DPICHANGED || msg == WM_DISPLAYCHANGE || msg == WM_SETTINGCHANGE)
	{
		if (msg == WM_DPICHANGED)
			barUISet.PublishWindowDpi(LOWORD(wParam));
		else
			(void)Inkeys::Display::Refresh(msg == WM_DISPLAYCHANGE
				? Inkeys::Display::ChangeReason::Display
				: Inkeys::Display::ChangeReason::Settings);
		return 0;
	}

	if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
	{
		if (setlist.regularSetting.clickRecover && ConfirmaNoMouMsgSignal)
			ConfirmaNoMouMsgSignal = false;
	}

	switch (msg)
	{
	case WM_MOUSEACTIVATE:
		// Bar 从创建起不激活；返回该值仍会继续投递本次鼠标点击。
		return MA_NOACTIVATE;

	case WM_INPUT:
	{
		// Raw Input 只负责唤醒并读取系统光标，WM_INPUT 仍交给默认过程完成清理。
		barUISet.RegisterBorderCursorLight(hWnd);
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	case WM_TIMER:
	{
		if (wParam == BarBorderCursorGraceTimerId)
		{
			barUISet.HandleBorderCursorGraceTimeout(hWnd);
			return 0;
		}
		if (wParam == BarThicknessAnnotationTooltipGraceTimerId
			|| wParam == BarThicknessOverflowTooltipGraceTimerId)
		{
			bool annotation =
				wParam == BarThicknessAnnotationTooltipGraceTimerId;
			UINT_PTR timerId = annotation
				? BarThicknessAnnotationTooltipGraceTimerId
				: BarThicknessOverflowTooltipGraceTimerId;
			KillTimer(hWnd, timerId);

			auto& drawAttribute = barUISet.barState.drawAttributeBar;
			IdtAtomic<bool>& grace = annotation
				? drawAttribute.thicknessAnnotationHoverGrace
				: drawAttribute.thicknessOverflowHoverGrace;
			IdtAtomic<bool>& hover = annotation
				? drawAttribute.thicknessAnnotationHover
				: drawAttribute.thicknessOverflowHover;
			IdtAtomic<bool>& pinned = annotation
				? drawAttribute.thicknessAnnotationPinned
				: drawAttribute.thicknessOverflowPinned;
			if (!static_cast<bool>(grace))
				return 0;

			bool popupInteractive = static_cast<bool>(hover)
				|| static_cast<bool>(pinned) || static_cast<bool>(grace);
			grace = false;

			POINT point{};
			bool pointAvailable = GetCursorPos(&point)
				&& BarScreenToLayout(point);
			if (pointAvailable)
				point.y = barUISet.BottomDockRigidHitTestY(point.y);
			auto infoHit = barUISet.shapeMap[annotation
				? BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit
				: BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit];
			auto popup = barUISet.shapeMap[annotation
				? BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup
				: BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup];
				bool available = barUISet.barState.drawAttribute
					&& !barUISet.barState.fold
					&& (annotation
						? static_cast<bool>(drawAttribute.penTypeMenuOpen)
							&& PenModeSupportsAnnotationLine(
								stateMode.Pen.ModeSelect)
						: static_cast<bool>(
							drawAttribute.thicknessOverflowHintPresent));
				// 颜色选择器盖住下方控件时，宽限期计时器也不得再把悬停还给滑块/提示。
				auto colorPickerPanel = barUISet.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
				bool colorPickerOccludes = pointAvailable
					&& barUISet.barState.drawAttributeBar.colorPickerOpen
					&& colorPickerPanel
					&& colorPickerPanel->IsClick(
						point.x, point.y, barUISet.barStyle.zoom);
				bool pointerInside = pointAvailable && available
					&& !colorPickerOccludes
					&& ((infoHit && infoHit->IsClick(
						point.x, point.y, barUISet.barStyle.zoom))
						|| (popupInteractive && popup && popup->IsClick(
							point.x, point.y, barUISet.barStyle.zoom)));
				bool changed = static_cast<bool>(hover) != pointerInside;
				hover = pointerInside;

				auto sliderHit = barUISet.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderHit];
				bool sliderAvailable = stateMode.StateModeSelect
					== StateModeSelectEnum::IdtPen
					&& barUISet.barState.drawAttribute && !barUISet.barState.fold
					&& GetBarThicknessSliderRange(
						stateMode.Pen.ModeSelect,
						barUISet.barStyle.dpiZoom).supported;
				bool sliderHover = sliderAvailable
					&& drawAttribute.thicknessViewMode
						!= ThicknessViewMode::FineDial
					&& !drawAttribute.thicknessSliderCapture
					&& !colorPickerOccludes
					&& !pointerInside
					&& pointAvailable
					&& ((sliderHit && sliderHit->IsClick(
						point.x, point.y, barUISet.barStyle.zoom))
						|| IsBarThicknessPreviewPopupHit(
							barUISet, point.x, point.y)
						|| IsBarThicknessPrecisionDragHit(
							barUISet, point.x, point.y));
			if (static_cast<bool>(drawAttribute.thicknessSliderHover)
				!= sliderHover)
			{
				drawAttribute.thicknessSliderHover = sliderHover;
				if (sliderHover
					&& drawAttribute.thicknessViewMode
						== ThicknessViewMode::Preview)
					drawAttribute.thicknessViewMode = ThicknessViewMode::Slider;
				else if (!sliderHover
					&& drawAttribute.thicknessViewMode
						== ThicknessViewMode::Slider
					&& !drawAttribute.thicknessSliderPinned
					&& !drawAttribute.thicknessSliderPressed
					&& !drawAttribute.thicknessSliderDragging)
					drawAttribute.thicknessViewMode = ThicknessViewMode::Preview;
				changed = true;
			}
			if (changed) barUISet.UpdateRendering(false);
			return 0;
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
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

	case BarThicknessSliderCaptureMessage:
	{
		if (wParam == BarThicknessSliderCaptureStart)
		{
			// 捕获必须由窗口线程建立，保证鼠标移出 Bar 后仍继续投递。
			SetCapture(hWnd);
			bool captured = GetCapture() == hWnd;
			barUISet.barState.drawAttributeBar.thicknessSliderCapture =
				captured;
			return captured ? 1 : 0;
		}

		barUISet.barState.drawAttributeBar.thicknessSliderCapture = false;
		if (GetCapture() == hWnd) ReleaseCapture();
		if (wParam == BarThicknessSliderCaptureCancel)
			QueueBarThicknessSliderEnd(hWnd);
		return 0;
	}

	case BarColorPickerCaptureMessage:
	{
		if (wParam == BarColorPickerCaptureStart)
		{
			// 与粗细调节一致，由窗口线程建立捕获，允许拖出色板继续夹紧选色。
			SetCapture(hWnd);
			bool captured = GetCapture() == hWnd;
			barUISet.barState.drawAttributeBar.colorPickerPointerCapture = captured;
			return captured ? 1 : 0;
		}

		barUISet.barState.drawAttributeBar.colorPickerPointerCapture = false;
		if (GetCapture() == hWnd) ReleaseCapture();
		if (wParam == BarColorPickerCaptureCancel)
			QueueBarColorPickerEnd(hWnd);
		return 0;
	}

	case WM_CAPTURECHANGED:
	{
		if (barUISet.barState.drawAttributeBar.colorPickerPointerCapture)
		{
			auto& picker = barUISet.barState.drawAttributeBar;
			picker.colorPickerPointerCapture = false;
			picker.colorPickerPointerPressed = false;
			picker.colorPickerHoldHintActive = false;
			picker.colorPickerHoldLocked = false;
			picker.colorPickerHoldProgress = 0.0f;
			QueueBarColorPickerEnd(hWnd);
			barUISet.UpdateRendering(false);
			return 0;
		}
		if (barUISet.barState.drawAttributeBar.thicknessSliderCapture)
		{
			// 捕获被其他窗口夺走时，合成抬起事件唤醒阻塞式手势循环。
				barUISet.barState.drawAttributeBar.thicknessViewMode =
					ThicknessViewMode::Preview;
				barUISet.barState.drawAttributeBar.thicknessSliderCapture = false;
				barUISet.barState.drawAttributeBar.thicknessSliderHover = false;
				barUISet.barState.drawAttributeBar.thicknessSliderPinned = false;
				barUISet.barState.drawAttributeBar.thicknessSliderDragging = false;
				barUISet.barState.drawAttributeBar.thicknessPreviewDragging = false;
				barUISet.barState.drawAttributeBar.thicknessSliderPressed = false;
				barUISet.barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
				barUISet.barState.drawAttributeBar.thicknessFineDialVisualWidth = 0.0f;
				barUISet.barState.drawAttributeBar.thicknessFineDialCandidateActive = false;
				barUISet.barState.drawAttributeBar.thicknessFineDialDragging = false;
				barUISet.barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
				barUISet.barState.drawAttributeBar
					.thicknessFineDialRangeTransitionActive = false;
				barUISet.barState.drawAttributeBar
					.thicknessFineDialActivationPreviewActive = false;
				barUISet.barState.drawAttributeBar
					.thicknessFineDialActivationDwellActive = false;
				barUISet.barState.drawAttributeBar
					.thicknessFineDialActivationPreviewProgress = 0.0f;
				barUISet.barState.drawAttributeBar
					.thicknessFineDialActivationPreviewVisualWidth = 0.0f;
				barUISet.barState.drawAttributeBar
					.thicknessFineDialPopupExitLatchRequested = false;
				barUISet.barState.drawAttributeBar.thicknessSliderHoldHintActive = false;
				barUISet.barState.drawAttributeBar.thicknessSliderHoldLocked = false;
				barUISet.barState.drawAttributeBar.thicknessSliderHoldProgress = 0.0f;
				barUISet.CloseDrawAttributeTooltips();
				QueueBarThicknessSliderEnd(hWnd);
				barUISet.UpdateRendering(false);
				return 0;
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	case WM_MOUSELEAVE:
	{
		// 需要等待离开的休眠路径在真实移出后解除重新激活限制。
		{
			lock_guard lock(barUISet.borderCursorLightMutex);
			barUISet.borderCursorActivationBlockedUntilLeave = false;
		}
		if (barUISet.barState.drawAttributeBar.thicknessSliderHover
			&& !barUISet.barState.drawAttributeBar.thicknessSliderPinned
			&& !barUISet.barState.drawAttributeBar.thicknessSliderPressed
			&& !barUISet.barState.drawAttributeBar.thicknessSliderDragging)
		{
			barUISet.barState.drawAttributeBar.thicknessSliderHover = false;
			if (barUISet.barState.drawAttributeBar.thicknessViewMode
				== ThicknessViewMode::Slider)
				barUISet.barState.drawAttributeBar.thicknessViewMode =
					ThicknessViewMode::Preview;
			barUISet.UpdateRendering(false);
		}
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
							MarkBarTouchPointerMessage(msgMouse, true, true);

							(void)Inkeys::Window::Enqueue(hWnd, msgMouse);
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
							MarkBarTouchPointerMessage(msgMouse, false, true);

							(void)Inkeys::Window::Enqueue(hWnd, msgMouse);
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
							MarkBarTouchPointerMessage(msgMouse, false, true);

							(void)Inkeys::Window::Enqueue(hWnd, msgMouse);
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
							MarkBarTouchPointerMessage(msgMouse, false, true);

							(void)Inkeys::Window::Enqueue(hWnd, msgMouse);
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
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
	{
		// Pen 与 Touch 均由 WM_TOUCH 合成；系统兼容鼠标副本不能再次进入 Bar。
		if (Inkeys::Message::IsPointerGeneratedMouseMessage(
			msg, static_cast<ULONG_PTR>(GetMessageExtraInfo())))
			return 0;
		if (msg == WM_MOUSEMOVE) barUISet.ActivateBorderCursorTracking(hWnd);
		if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, true);
		if (msg == WM_LBUTTONUP) Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);

		// 否则当成真正的鼠标消息处理

		break;
	}

	default:
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// UI 总集
void BarUISetClass::CloseAnnotationTooltip()
{
	if (floating_window && IsWindow(floating_window))
		KillTimer(floating_window, BarThicknessAnnotationTooltipGraceTimerId);
	barState.drawAttributeBar.thicknessAnnotationHover = false;
	barState.drawAttributeBar.thicknessAnnotationHoverGrace = false;
	barState.drawAttributeBar.thicknessAnnotationPinned = false;
	barState.drawAttributeBar.thicknessAnnotationClosePress = false;
}

void BarUISetClass::CloseThicknessOverflowTooltip()
{
	if (floating_window && IsWindow(floating_window))
		KillTimer(floating_window, BarThicknessOverflowTooltipGraceTimerId);
	barState.drawAttributeBar.thicknessOverflowHover = false;
	barState.drawAttributeBar.thicknessOverflowHoverGrace = false;
	barState.drawAttributeBar.thicknessOverflowPinned = false;
	barState.drawAttributeBar.thicknessOverflowClosePress = false;
}

void BarUISetClass::CloseDrawAttributeTooltips()
{
	CloseAnnotationTooltip();
	CloseThicknessOverflowTooltip();
}

void BarUISetClass::ClosePenTypeMenu()
{
	barState.drawAttributeBar.penTypeMenuOpen = false;
	barState.drawAttributeBar.penTypeExtensionPress = false;
	barState.drawAttributeBar.penTypeFreeLinePress = false;
	drawAttributePenTypeExtensionHoverStage = BarButtonHoverStageEnum::None;
	drawAttributePenTypeFreeLineHoverStage = BarButtonHoverStageEnum::None;
	// 菜单退场时同步清除问号的宽限期、固定态和命中区域状态。
	CloseAnnotationTooltip();
}

void BarUISetClass::CloseThicknessSlider(bool cancelCapture)
{
	bool gestureActive =
		barState.drawAttributeBar.thicknessSliderPressed
		|| barState.drawAttributeBar.thicknessSliderDragging
		|| barState.drawAttributeBar.thicknessPreviewDragging
		|| barState.drawAttributeBar.thicknessFineDialDragging
		|| barState.drawAttributeBar.thicknessFineDialPhysicsActive
		|| barState.drawAttributeBar.thicknessSliderCapture;
	barState.drawAttributeBar.thicknessViewMode = ThicknessViewMode::Preview;
	barState.drawAttributeBar.thicknessSliderHover = false;
		barState.drawAttributeBar.thicknessSliderPinned = false;
		barState.drawAttributeBar.thicknessSliderDragging = false;
		barState.drawAttributeBar.thicknessPreviewDragging = false;
		barState.drawAttributeBar.thicknessSliderPressed = false;
		barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
		barState.drawAttributeBar.thicknessFineDialVisualWidth = 0.0f;
		barState.drawAttributeBar.thicknessFineDialCandidateActive = false;
		barState.drawAttributeBar.thicknessFineDialDragging = false;
		barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
		barState.drawAttributeBar.thicknessFineDialRangeTransitionActive = false;
		barState.drawAttributeBar.thicknessFineDialActivationPreviewActive = false;
		barState.drawAttributeBar.thicknessFineDialActivationDwellActive = false;
		barState.drawAttributeBar.thicknessFineDialActivationPreviewProgress = 0.0f;
		barState.drawAttributeBar.thicknessFineDialActivationPreviewVisualWidth = 0.0f;
		barState.drawAttributeBar.thicknessFineDialPopupExitLatchRequested = false;
		barState.drawAttributeBar.thicknessSliderHoldHintActive = false;
		barState.drawAttributeBar.thicknessSliderHoldLocked = false;
		barState.drawAttributeBar.thicknessSliderHoldProgress = 0.0f;

		if (floating_window && IsWindow(floating_window)
			&& (barState.drawAttributeBar.thicknessSliderCapture
				|| (cancelCapture && gestureActive)))
		{
			SendMessage(floating_window, BarThicknessSliderCaptureMessage,
				cancelCapture
					? BarThicknessSliderCaptureCancel
					: BarThicknessSliderCaptureStop,
			0);
	}
	else barState.drawAttributeBar.thicknessSliderCapture = false;
}

void BarUISetClass::CloseColorPicker(bool cancelCapture)
{
	auto& picker = barState.drawAttributeBar;
	bool gestureActive = picker.colorPickerPointerPressed
		|| picker.colorPickerPointerCapture;
	picker.colorPickerOpen = false;
	picker.colorPickerPointerPressed = false;
	picker.colorPickerHoldHintActive = false;
	picker.colorPickerHoldLocked = false;
	picker.colorPickerHoldProgress = 0.0f;
	picker.colorPickerTonePress = false;
	picker.colorPickerClosePress = false;
	picker.colorPickerKeyboardDownMask = 0;
	if (floating_window && IsWindow(floating_window))
	{
		if (picker.colorPickerPointerCapture || (cancelCapture && gestureActive))
		{
			SendMessage(floating_window, BarColorPickerCaptureMessage,
				cancelCapture
					? BarColorPickerCaptureCancel
					: BarColorPickerCaptureStop,
				0);
			return;
		}
	}
	picker.colorPickerPointerCapture = false;
}

void BarUISetClass::ShutdownWindowInput(HWND hWnd)
{
	if (!hWnd) return;

	// 先撤销输入源，避免 ReleaseCapture 重入时继续向交互队列投递手势。
	KillTimer(hWnd, BarBorderCursorGraceTimerId);
	KillTimer(hWnd, BarThicknessAnnotationTooltipGraceTimerId);
	KillTimer(hWnd, BarThicknessOverflowTooltipGraceTimerId);
	barState.drawAttributeBar.thicknessSliderCapture = false;
	barState.drawAttributeBar.colorPickerPointerCapture = false;
	CloseThicknessSlider(false);
	CloseColorPicker(false);
	ClosePenTypeMenu();
	CloseDrawAttributeTooltips();
	if (GetCapture() == hWnd) ReleaseCapture();
	SuspendBorderCursorTracking(hWnd);
}

namespace
{
struct BarInteractionMemberAccess
{
	BarSeekResult (BarUISetClass::*seek)(const ExMessage&) = nullptr;
	void (BarUISetClass::*closeThicknessOverflowTooltip)() = nullptr;
	void (BarUISetClass::*closeDrawAttributeTooltips)() = nullptr;
	void (BarUISetClass::*closePenTypeMenu)() = nullptr;
	void (BarUISetClass::*closeThicknessSlider)(bool) = nullptr;
	void (BarUISetClass::*closeColorPicker)(bool) = nullptr;
};

enum class BarInteractionStageResult : int
{
	PassThrough,
	Consumed,
	Shutdown,
};

// 单次交互线程会话持有临时状态，BarUISetClass 只保留稳定产品状态。
class BarInteractionSession final
{
private:
	enum class IndependentHoverTargetEnum
	{
		None,
		DrawAttributeBrush,
		DrawAttributeHighlight,
		DrawAttributePenTypeExtension,
		DrawAttributePenTypeFreeLine,
		DrawAttributeThicknessFine,
		DrawAttributeThicknessMedium,
		DrawAttributeThicknessCoarse,
		DrawAttributeThicknessAdjust,
		DrawAttributeAnnotationClose,
		DrawAttributeOverflowClose,
		DrawAttributeColorPickerTone,
		DrawAttributeColorPickerClose,
		MoreClose,
		GeometryStraightLine,
		GeometryRectangle,
		GeometryThicknessFine,
		GeometryThicknessMedium,
		GeometryThicknessCoarse,
		GeometryClose,
	};

	struct HoverVisualRef
	{
		BarUiPctClass* pct = nullptr;
		BarUiColorClass* fill = nullptr;
		IdtAtomic<BarButtonHoverStageEnum>* stage = nullptr;
	};

	enum class ThicknessFineDialPhase : int
	{
		Idle,
		Dragging,
		Inertia,
		Settling,
	};

	struct ThicknessFineDialVelocitySample
	{
		double screenX = 0.0;
		ULONGLONG tick = 0;
	};

public:
	BarInteractionSession(BarUISetClass& owner,
		std::atomic<unsigned long long>& clickPulseSerial,
		BarInteractionMemberAccess access)
		: barUISet(owner),
		barButtonSet(owner.barButtonSet),
		barState(owner.barState),
		barStyle(owner.barStyle),
		shapeMap(owner.shapeMap),
		superellipseMap(owner.superellipseMap),
		drawAttributeBrushHoverStage(owner.drawAttributeBrushHoverStage),
		drawAttributeHighlightHoverStage(owner.drawAttributeHighlightHoverStage),
		drawAttributePenTypeExtensionHoverStage(
			owner.drawAttributePenTypeExtensionHoverStage),
		drawAttributePenTypeFreeLineHoverStage(
			owner.drawAttributePenTypeFreeLineHoverStage),
		drawAttributeThicknessFineHoverStage(
			owner.drawAttributeThicknessFineHoverStage),
		drawAttributeThicknessMediumHoverStage(
			owner.drawAttributeThicknessMediumHoverStage),
		drawAttributeThicknessCoarseHoverStage(
			owner.drawAttributeThicknessCoarseHoverStage),
		drawAttributeThicknessAdjustHoverStage(
			owner.drawAttributeThicknessAdjustHoverStage),
		drawAttributeAnnotationCloseHoverStage(
			owner.drawAttributeAnnotationCloseHoverStage),
		drawAttributeOverflowCloseHoverStage(
			owner.drawAttributeOverflowCloseHoverStage),
		drawAttributeColorPickerToneHoverStage(
			owner.drawAttributeColorPickerToneHoverStage),
		drawAttributeColorPickerCloseHoverStage(
			owner.drawAttributeColorPickerCloseHoverStage),
		moreCloseHoverStage(owner.moreCloseHoverStage),
		geometryStraightLineHoverStage(owner.geometryStraightLineHoverStage),
		geometryRectangleHoverStage(owner.geometryRectangleHoverStage),
		geometryThicknessFineHoverStage(owner.geometryThicknessFineHoverStage),
		geometryThicknessMediumHoverStage(owner.geometryThicknessMediumHoverStage),
		geometryThicknessCoarseHoverStage(owner.geometryThicknessCoarseHoverStage),
		geometryCloseHoverStage(owner.geometryCloseHoverStage),
		mainButtonClickPulseSerial(clickPulseSerial),
		memberAccess(access)
	{
	}

private:
	HoverVisualRef GetIndependentHoverVisual(IndependentHoverTargetEnum target)
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
			case IndependentHoverTargetEnum::DrawAttributePenTypeExtension:
				return { &shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit]->pct,
					&shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit]
						->fill.value(),
					&drawAttributePenTypeExtensionHoverStage };
			case IndependentHoverTargetEnum::DrawAttributePenTypeFreeLine:
				return { &shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine]->pct,
					&shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine]
						->fill.value(),
					&drawAttributePenTypeFreeLineHoverStage };
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
			case IndependentHoverTargetEnum::DrawAttributeColorPickerTone:
				return { &shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle]->pct,
					&shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle]
						->fill.value(),
					&drawAttributeColorPickerToneHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeColorPickerClose:
				return { &shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit]->pct,
					&shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit]
							->fill.value(),
					&drawAttributeColorPickerCloseHoverStage };
			case IndependentHoverTargetEnum::MoreClose:
				return { &shapeMap[BarUISetShapeEnum::MorePanelCloseHit]->pct,
					&shapeMap[BarUISetShapeEnum::MorePanelCloseHit]->fill.value(),
					&moreCloseHoverStage };
			case IndependentHoverTargetEnum::GeometryStraightLine:
				return { &shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_StraightLine]->pct,
					&shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_StraightLine]->fill.value(),
					&geometryStraightLineHoverStage };
			case IndependentHoverTargetEnum::GeometryRectangle:
				return { &shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_Rectangle]->pct,
					&shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_Rectangle]->fill.value(),
					&geometryRectangleHoverStage };
			case IndependentHoverTargetEnum::GeometryThicknessFine:
				return { &shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine]->pct,
					&shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine]->fill.value(),
					&geometryThicknessFineHoverStage };
			case IndependentHoverTargetEnum::GeometryThicknessMedium:
				return { &shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium]->pct,
					&shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium]->fill.value(),
					&geometryThicknessMediumHoverStage };
			case IndependentHoverTargetEnum::GeometryThicknessCoarse:
				return { &shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse]->pct,
					&shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse]->fill.value(),
					&geometryThicknessCoarseHoverStage };
			case IndependentHoverTargetEnum::GeometryClose:
				return { &shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_Close]->pct,
					&shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_Close]->fill.value(),
					&geometryCloseHoverStage };
			default:
				return {};
			}
		}

	void StartHover(BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtonHoverStageEnum>* hoverStage)
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
			*hoverStage = BarButtonHoverStageEnum::Showing;
			UpdateRendering(false);
		}

	void StopHover(BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtonHoverStageEnum>* hoverStage, bool immediate,
		bool preserveVisual = false)
		{
			if (!hoverPct || !hoverStage) return;
			if (immediate)
			{
				*hoverStage = BarButtonHoverStageEnum::None;
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
				*hoverStage = BarButtonHoverStageEnum::Fading;
				const BarUiCurveSpecClass hoverExitCurve{
					BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
				hoverPct->SetTar(
					0.0, BarButtonHoverExitDur, nullopt, true, hoverExitCurve);
			}
			UpdateRendering(false);
		}

	void StartMainBarButtonHover(BarButtonClass* button)
		{
			if (button && button->preset != BarButtonPresetEnum::Divider
				&& button->button.fill.has_value())
				StartHover(&button->button.pct, &button->button.fill.value(), &button->hoverStage);
		}

	void StopMainBarButtonHover(
		BarButtonClass* button, bool immediate, bool preserveVisual = false)
		{
			if (!button) return;
			if (button->preset == BarButtonPresetEnum::Divider)
			{
				// 不通过 StopHover 清零 Shape 透明度，否则会连分隔线本体一起隐藏。
				button->hoverStage = BarButtonHoverStageEnum::None;
				button->state->emph = BarWidgetEmphasize::None;
				button->pressScale.SetDirect(1.0);
				button->button.pct.animateWhenDisabled = false;
				if (button->button.fill.has_value())
					button->button.fill.value().animateWhenDisabled = false;
				return;
			}
			StopHover(&button->button.pct,
				button->button.fill.has_value() ? &button->button.fill.value() : nullptr,
				&button->hoverStage, immediate, preserveVisual);
		}

	void StartIndependentHover(IndependentHoverTargetEnum target)
		{
			auto hover = GetIndependentHoverVisual(target);
			StartHover(hover.pct, hover.fill, hover.stage);
		}

	void StopIndependentHover(IndependentHoverTargetEnum target, bool immediate,
		bool preserveVisual = false)
		{
			auto hover = GetIndependentHoverVisual(target);
			// 与主栏一致：移出快速退出，按下时从当前悬停视觉连续衔接。
			StopHover(hover.pct, hover.fill, hover.stage, immediate, preserveVisual);
		}

	void SuppressHoverUntilPointerMove()
		{
			POINT point{};
			if (GetCursorPos(&point))
			{
				hoverSuppressionScreenPoint = point;
				suppressHoverUntilPointerMove = true;
			}
		}

	bool ThicknessSliderAvailable()
		{
			return stateMode.StateModeSelect
				== StateModeSelectEnum::IdtPen
				&& barState.drawAttribute && !barState.fold
				&& GetBarThicknessSliderRange(
					stateMode.Pen.ModeSelect,
					barStyle.dpiZoom).supported;
		};
	void ResetThicknessFineDialSamples()
		{
			thicknessFineDialSampleCount = 0;
			thicknessFineDialSamples = {};
		}

	void AddThicknessFineDialSample(double screenX, ULONGLONG tick)
		{
			if (thicknessFineDialSampleCount
				< thicknessFineDialSamples.size())
			{
				thicknessFineDialSamples[thicknessFineDialSampleCount++] =
					{ screenX, tick };
				return;
			}
			for (size_t index = 1;
				index < thicknessFineDialSamples.size(); ++index)
				thicknessFineDialSamples[index - 1] =
					thicknessFineDialSamples[index];
			thicknessFineDialSamples.back() = { screenX, tick };
		}

	double EstimateThicknessFineDialScreenVelocity()
		{
			if (thicknessFineDialSampleCount < 2) return 0.0;
			ULONGLONG newestTick = thicknessFineDialSamples[
				thicknessFineDialSampleCount - 1].tick;
			double weightedVelocity = 0.0;
			double totalWeight = 0.0;
			for (size_t index = 1;
				index < thicknessFineDialSampleCount; ++index)
			{
				const auto& previous = thicknessFineDialSamples[index - 1];
				const auto& current = thicknessFineDialSamples[index];
				if (newestTick - previous.tick
					> BarThicknessFineDialVelocityWindowMs
					|| current.tick <= previous.tick)
					continue;
				double segmentSeconds = static_cast<double>(
					current.tick - previous.tick) / 1000.0;
				double weight = static_cast<double>(index);
				weightedVelocity += (current.screenX - previous.screenX)
					/ segmentSeconds * weight;
				totalWeight += weight;
			}
			return totalWeight > 0.0
				? weightedVelocity / totalWeight : 0.0;
		}

	double ProjectThicknessFineDialRubberBand(double rawValue)
		{
			double dpiScale = max(1.0,
				static_cast<double>(barStyle.dpiZoom));
			double limitValue = BarThicknessFineDialRubberBandLimitDip
				* dpiScale / max(0.000001,
					thicknessFineDialUnitTravelScreen);
			if (rawValue < thicknessFineDialRangeMin)
			{
				double overshoot = thicknessFineDialRangeMin - rawValue;
				return thicknessFineDialRangeMin
					- limitValue * (1.0 - exp(-overshoot / limitValue));
			}
			if (rawValue > thicknessFineDialRangeMax)
			{
				double overshoot = rawValue - thicknessFineDialRangeMax;
				return thicknessFineDialRangeMax
					+ limitValue * (1.0 - exp(-overshoot / limitValue));
			}
			return rawValue;
		}

	int PublishThicknessFineDialCandidate()
		{
			thicknessFineDialVisualValue =
				ProjectThicknessFineDialRubberBand(
					thicknessFineDialRawValue);
			int candidate = clamp(static_cast<int>(lround(
				thicknessFineDialVisualValue)),
				thicknessFineDialRangeMin,
				thicknessFineDialRangeMax);
			barState.drawAttributeBar.thicknessFineDialVisualWidth =
				static_cast<float>(thicknessFineDialVisualValue);
			barState.drawAttributeBar.thicknessSliderCandidateWidth =
				static_cast<float>(candidate);
			barState.drawAttributeBar.thicknessFineDialCandidateActive = true;
			return candidate;
		}

	void CancelThicknessFineDialSelection()
		{
			thicknessFineDialPhase = ThicknessFineDialPhase::Idle;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialResidualVelocity = 0.0;
			thicknessFineDialSettleTarget = 0.0;
			thicknessFineDialCommitIssued = false;
			ResetThicknessFineDialSamples();
			barState.drawAttributeBar.thicknessFineDialDragging = false;
			barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
			barState.drawAttributeBar.thicknessFineDialCandidateActive = false;
			barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
			barState.drawAttributeBar.thicknessSliderHoldHintActive = false;
			barState.drawAttributeBar.thicknessSliderHoldLocked = false;
			barState.drawAttributeBar.thicknessSliderHoldProgress = 0.0f;
		}

	void CommitThicknessFineDialSelection()
		{
			bool candidateActive = barState.drawAttributeBar
				.thicknessFineDialCandidateActive;
			int candidate = clamp(static_cast<int>(lround(
				static_cast<double>(barState.drawAttributeBar
					.thicknessSliderCandidateWidth))),
				thicknessFineDialRangeMin,
				thicknessFineDialRangeMax);
			bool shouldCommit = candidateActive
				&& !thicknessFineDialCommitIssued
				&& abs(static_cast<double>(GetPenWidth()) - candidate)
					> 0.000001;
			thicknessFineDialCommitIssued = true;
			thicknessFineDialPhase = ThicknessFineDialPhase::Idle;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialResidualVelocity = 0.0;
			ResetThicknessFineDialSamples();
			barState.drawAttributeBar.thicknessFineDialDragging = false;
			barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
			if (shouldCommit)
				SetPenWidth(static_cast<float>(candidate), true);
			barState.drawAttributeBar.thicknessFineDialCandidateActive = false;
			barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
			thicknessFineDialCommitIssued = false;
		}

	void BeginThicknessFineDialDrag(double startValue,
		double screenX, double unitTravelScreen,
		const BarThicknessSliderRange& range)
		{
			bool continuingMotion = thicknessFineDialPhase
				== ThicknessFineDialPhase::Inertia
				|| thicknessFineDialPhase
					== ThicknessFineDialPhase::Settling;
			thicknessFineDialResidualVelocity =
				thicknessFineDialPhase == ThicknessFineDialPhase::Inertia
					? thicknessFineDialVelocity : 0.0;
			if (continuingMotion
				&& barState.drawAttributeBar.thicknessFineDialCandidateActive)
				startValue = static_cast<double>(barState.drawAttributeBar
					.thicknessFineDialVisualWidth);
			thicknessFineDialRangeMin = range.min;
			thicknessFineDialRangeMax = range.max;
			thicknessFineDialUnitTravelScreen = max(0.000001,
				unitTravelScreen);
			thicknessFineDialRawValue = startValue;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialGrabTick = GetTickCount64();
			thicknessFineDialCommitIssued = false;
			thicknessFineDialPhase = ThicknessFineDialPhase::Dragging;
			barState.drawAttributeBar.thicknessFineDialDragging = true;
			barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
			ResetThicknessFineDialSamples();
			AddThicknessFineDialSample(
				screenX, thicknessFineDialGrabTick);
			PublishThicknessFineDialCandidate();
		}

	void EndThicknessFineDialDrag(bool holdLocked)
		{
			barState.drawAttributeBar.thicknessFineDialDragging = false;
			if (holdLocked)
			{
				CommitThicknessFineDialSelection();
				return;
			}
			double sampledScreenVelocity =
				EstimateThicknessFineDialScreenVelocity();
			double heldSeconds = static_cast<double>(
				GetTickCount64() - thicknessFineDialGrabTick) / 1000.0;
			double residualVelocity = thicknessFineDialResidualVelocity
				* exp(-BarThicknessFineDialResidualDecayPerSecond
					* max(0.0, heldSeconds));
			// Pointer 向右时 value 递减，确保刻度内容与手指同向移动。
			double sampledValueVelocity = -sampledScreenVelocity
				/ max(0.000001, thicknessFineDialUnitTravelScreen);
			thicknessFineDialVelocity = sampledValueVelocity
				+ BarThicknessFineDialResidualWeight * residualVelocity;
			double dpiScale = max(1.0,
				static_cast<double>(barStyle.dpiZoom));
			double maximumValueVelocity =
				BarThicknessFineDialMaxVelocityDipPerSecond * dpiScale
				/ max(0.000001, thicknessFineDialUnitTravelScreen);
			thicknessFineDialVelocity = clamp(
				thicknessFineDialVelocity,
				-maximumValueVelocity, maximumValueVelocity);
			double releaseThresholdValue =
				BarThicknessFineDialReleaseVelocityDipPerSecond * dpiScale
				/ max(0.000001, thicknessFineDialUnitTravelScreen);
			if (thicknessFineDialRawValue < thicknessFineDialRangeMin
				|| thicknessFineDialRawValue > thicknessFineDialRangeMax)
			{
				thicknessFineDialSettleTarget = clamp(
					thicknessFineDialRawValue,
					static_cast<double>(thicknessFineDialRangeMin),
					static_cast<double>(thicknessFineDialRangeMax));
				thicknessFineDialPhase = ThicknessFineDialPhase::Settling;
			}
			else if (abs(thicknessFineDialVelocity)
				>= releaseThresholdValue)
				thicknessFineDialPhase = ThicknessFineDialPhase::Inertia;
			else
			{
				thicknessFineDialSettleTarget = clamp(
					static_cast<double>(lround(thicknessFineDialRawValue)),
					static_cast<double>(thicknessFineDialRangeMin),
					static_cast<double>(thicknessFineDialRangeMax));
				thicknessFineDialPhase = ThicknessFineDialPhase::Settling;
			}
			barState.drawAttributeBar.thicknessFineDialPhysicsActive = true;
			thicknessFineDialLastPhysicsTime = chrono::steady_clock::now();
			thicknessFineDialPhysicsClockNeedsReset = true;
		}

	void AdvanceThicknessFineDialPhysics()
		{
			if (!barState.drawAttributeBar.thicknessFineDialPhysicsActive
				|| barState.drawAttributeBar.thicknessViewMode
					!= ThicknessViewMode::FineDial
				|| !ThicknessSliderAvailable())
			{
				CancelThicknessFineDialSelection();
				return;
			}
			auto range = GetBarThicknessSliderRange(
				stateMode.Pen.ModeSelect, barStyle.dpiZoom);
			if (!range.supported || range.min != thicknessFineDialRangeMin
				|| range.max != thicknessFineDialRangeMax)
			{
				// 支持笔型或 DPI 量程变化时旧候选失效，由现有粗细动画接管。
				CancelThicknessFineDialSelection();
				UpdateRendering(false);
				return;
			}
			auto now = chrono::steady_clock::now();
			if (thicknessFineDialPhysicsClockNeedsReset)
			{
				thicknessFineDialLastPhysicsTime = now;
				thicknessFineDialPhysicsClockNeedsReset = false;
				return;
			}
			double dt = chrono::duration<double>(
				now - thicknessFineDialLastPhysicsTime).count();
			thicknessFineDialLastPhysicsTime = now;
			if (!isfinite(dt) || dt <= 0.0) return;
			dt = min(dt, BarThicknessFineDialMaxDtSeconds);
			double dpiScale = max(1.0,
				static_cast<double>(barStyle.dpiZoom));
			double releaseThresholdValue =
				BarThicknessFineDialReleaseVelocityDipPerSecond * dpiScale
				/ max(0.000001, thicknessFineDialUnitTravelScreen);
			if (thicknessFineDialPhase == ThicknessFineDialPhase::Inertia)
			{
				thicknessFineDialRawValue +=
					thicknessFineDialVelocity * dt;
				thicknessFineDialVelocity *= exp(
					-BarThicknessFineDialFrictionPerSecond * dt);
				PublishThicknessFineDialCandidate();
				if (thicknessFineDialRawValue < thicknessFineDialRangeMin
					|| thicknessFineDialRawValue > thicknessFineDialRangeMax)
				{
					thicknessFineDialSettleTarget = clamp(
						thicknessFineDialRawValue,
						static_cast<double>(thicknessFineDialRangeMin),
						static_cast<double>(thicknessFineDialRangeMax));
					thicknessFineDialPhase =
						ThicknessFineDialPhase::Settling;
				}
				else if (abs(thicknessFineDialVelocity)
					< releaseThresholdValue)
				{
					thicknessFineDialSettleTarget = clamp(
						static_cast<double>(lround(
							thicknessFineDialRawValue)),
						static_cast<double>(thicknessFineDialRangeMin),
						static_cast<double>(thicknessFineDialRangeMax));
					thicknessFineDialPhase =
						ThicknessFineDialPhase::Settling;
				}
			}
			else if (thicknessFineDialPhase
				== ThicknessFineDialPhase::Settling)
			{
				double error = thicknessFineDialRawValue
					- thicknessFineDialSettleTarget;
				double acceleration =
					-BarThicknessFineDialSpringOmega
						* BarThicknessFineDialSpringOmega * error
					- 2.0 * BarThicknessFineDialSpringDampingRatio
						* BarThicknessFineDialSpringOmega
						* thicknessFineDialVelocity;
				thicknessFineDialVelocity += acceleration * dt;
				thicknessFineDialRawValue +=
					thicknessFineDialVelocity * dt;
				PublishThicknessFineDialCandidate();
				double positionDip = abs(thicknessFineDialRawValue
					- thicknessFineDialSettleTarget)
					* thicknessFineDialUnitTravelScreen / dpiScale;
				double velocityDip = abs(thicknessFineDialVelocity)
					* thicknessFineDialUnitTravelScreen / dpiScale;
				if (positionDip <= BarThicknessFineDialSettleDistanceDip
					&& velocityDip
						<= BarThicknessFineDialSettleVelocityDipPerSecond)
				{
					thicknessFineDialRawValue =
						thicknessFineDialSettleTarget;
					PublishThicknessFineDialCandidate();
					CommitThicknessFineDialSelection();
				}
			}
			UpdateRendering(false);
		}

	bool IsIndependentHoverAllowed(IndependentHoverTargetEnum target)
		{
			if (target == IndependentHoverTargetEnum::MoreClose)
				return barState.moreExpanded && !barState.fold
					&& !barState.moreClosePress;
			if (target >= IndependentHoverTargetEnum::GeometryStraightLine
				&& target <= IndependentHoverTargetEnum::GeometryClose)
			{
				if (!barState.geometryAttribute || barState.fold
					|| stateMode.StateModeSelect != StateModeSelectEnum::IdtShape)
					return false;
				switch (target)
				{
				case IndependentHoverTargetEnum::GeometryStraightLine:
					return stateMode.Shape.ModeSelect
						!= ShapeModeSelectEnum::IdtShapeStraightLine1;
				case IndependentHoverTargetEnum::GeometryRectangle:
					return stateMode.Shape.ModeSelect
						!= ShapeModeSelectEnum::IdtShapeRectangle1;
				case IndependentHoverTargetEnum::GeometryThicknessFine:
				case IndependentHoverTargetEnum::GeometryThicknessMedium:
				case IndependentHoverTargetEnum::GeometryThicknessCoarse:
				{
					size_t index = static_cast<size_t>(target)
						- static_cast<size_t>(
							IndependentHoverTargetEnum::GeometryThicknessFine);
					return static_cast<int>(lround(max(
						0.0f, stateMode.Pen.Brush1.width)))
						!= GetBarThicknessPresetPx(
							PenModeSelectEnum::IdtPenBrush1, index,
							barStyle.dpiZoom);
				}
				case IndependentHoverTargetEnum::GeometryClose:
					return true;
				default:
					return false;
				}
			}
			if (!barState.drawAttribute) return false;
			switch (target)
			{
			case IndependentHoverTargetEnum::DrawAttributeBrush:
				return stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1;
			case IndependentHoverTargetEnum::DrawAttributeHighlight:
				return stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1;
			case IndependentHoverTargetEnum::DrawAttributePenTypeExtension:
				return !barState.fold
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect)
					&& !barState.drawAttributeBar.penTypeExtensionPress;
			case IndependentHoverTargetEnum::DrawAttributePenTypeFreeLine:
				return !barState.fold
					&& barState.drawAttributeBar.penTypeMenuOpen
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect)
					&& !barState.drawAttributeBar.penTypeFreeLinePress;
case IndependentHoverTargetEnum::DrawAttributeThicknessFine:
				case IndependentHoverTargetEnum::DrawAttributeThicknessMedium:
				case IndependentHoverTargetEnum::DrawAttributeThicknessCoarse:
				{
					if (!PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect))
						return false;
					size_t index = static_cast<size_t>(target)
						- static_cast<size_t>(
							IndependentHoverTargetEnum::DrawAttributeThicknessFine);
					int displayedThickness =
						static_cast<int>(lround(max(0.0f, GetPenWidth())));
					return displayedThickness
						!= GetBarThicknessPresetPx(
							stateMode.Pen.ModeSelect, index, barStyle.dpiZoom);
				}
			case IndependentHoverTargetEnum::DrawAttributeThicknessAdjust:
				return ThicknessSliderAvailable()
					&& barState.drawAttributeBar.thicknessViewMode
						== ThicknessViewMode::Preview;
			case IndependentHoverTargetEnum::DrawAttributeAnnotationClose:
				return !barState.fold
					&& barState.drawAttributeBar.penTypeMenuOpen
					&& barState.drawAttributeBar.thicknessAnnotationPinned
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect);
			case IndependentHoverTargetEnum::DrawAttributeOverflowClose:
				return !barState.fold
					&& barState.drawAttributeBar.thicknessOverflowPinned
					&& barState.drawAttributeBar.thicknessOverflowHintPresent;
			case IndependentHoverTargetEnum::DrawAttributeColorPickerTone:
				return stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
					&& !barState.fold
					&& barState.drawAttributeBar.colorPickerOpen
					&& !barState.drawAttributeBar.colorPickerTonePress;
			case IndependentHoverTargetEnum::DrawAttributeColorPickerClose:
				return stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
					&& !barState.fold
					&& barState.drawAttributeBar.colorPickerOpen
					&& !barState.drawAttributeBar.colorPickerClosePress;
			default:
				return false;
			}
		}

	static bool SetTooltipFlag(IdtAtomic<bool>& flag, bool value)
		{
			if (static_cast<bool>(flag) == value) return false;
			flag = value;
			return true;
		}

	static void CancelTooltipHoverGrace(IdtAtomic<bool>& grace,
		UINT_PTR timerId)
		{
			if (!static_cast<bool>(grace)) return;
			grace = false;
			if (floating_window && IsWindow(floating_window))
				KillTimer(floating_window, timerId);
		}

	static bool UpdateTooltipHover(bool available, bool pointerInside,
		IdtAtomic<bool>& hover, IdtAtomic<bool>& pinned,
		IdtAtomic<bool>& grace, UINT_PTR timerId)
		{
			if (!available)
			{
				CancelTooltipHoverGrace(grace, timerId);
				return SetTooltipFlag(hover, false);
			}
			if (pointerInside)
			{
				CancelTooltipHoverGrace(grace, timerId);
				return SetTooltipFlag(hover, true);
			}
			if (static_cast<bool>(pinned))
			{
				CancelTooltipHoverGrace(grace, timerId);
				return SetTooltipFlag(hover, false);
			}
			if (!static_cast<bool>(hover)
				|| static_cast<bool>(grace))
				return false;

			if (floating_window && IsWindow(floating_window)
				&& SetTimer(floating_window, timerId,
					BarThicknessTooltipHoverGraceMs, nullptr))
			{
				grace = true;
				return false;
			}
			return SetTooltipFlag(hover, false);
		}

	bool AnnotationTooltipAvailable()
		{
			return barState.drawAttribute && !barState.fold
				&& barState.drawAttributeBar.penTypeMenuOpen
				&& PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
		}

	bool OverflowTooltipAvailable()
		{
			return barState.drawAttribute && !barState.fold
				&& barState.drawAttributeBar.thicknessViewMode
					!= ThicknessViewMode::FineDial
				&& barState.drawAttributeBar.thicknessOverflowHintPresent;
		}

	bool ColorPickerAvailable()
	{
		return stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
			&& barState.drawAttribute && !barState.fold;
	}

	bool IsColorPickerOccludingPoint(int clientX, int clientY)
	{
		if (!ColorPickerAvailable()
			|| !barState.drawAttributeBar.colorPickerOpen)
			return false;
		auto pickerPanel = shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
		return pickerPanel
			&& pickerPanel->IsClick(clientX, clientY, barStyle.zoom);
	}

	bool IsPenTypeMenuOccludingPoint(int clientX, int clientY)
	{
		if (!barState.drawAttributeBar.penTypeMenuOpen)
			return false;
		auto menu = shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu];
		return menu && menu->IsClick(clientX, clientY, barStyle.zoom);
	}

	static unsigned int ColorPickerKeyMask(BYTE vkCode)
		{
			switch (vkCode)
			{
			case VK_LEFT: case 'A': return 1u << 0;
			case VK_RIGHT: case 'D': return 1u << 1;
			case VK_UP: case 'W': return 1u << 2;
			case VK_DOWN: case 'S': return 1u << 3;
			default: return 0;
			}
		}

	void ApplyColorPickerPoint(double markerX, double markerY,
		bool setMemory)
		{
			if (markerX < 0.0 || markerX > 1.0)
				markerX = markerX - floor(markerX);
			markerY = clamp(markerY, 0.0, 1.0);
			COLORREF color = GetBarColorPickerColor(
				markerX >= 1.0 ? 0.0 : markerX, markerY,
				barState.drawAttributeBar.colorPickerDarkTone,
				barState.widgetPosition.primaryBar);
			barState.drawAttributeBar.colorPickerMarkerX =
				static_cast<float>(markerX);
			barState.drawAttributeBar.colorPickerMarkerY =
				static_cast<float>(markerY);
			barState.drawAttributeBar.colorPickerMarkerVisible = true;
			SetPenColor(Inkeys::Color::SetAlphaR(color, 255), setMemory);
		}

	void ProjectCurrentColorPickerPoint()
		{
			double x = 0.0, y = 0.0;
			bool exact = false;
			COLORREF currentColor = GetPenColor() & 0x00FFFFFF;
			ProjectBarColorPickerColor(currentColor,
				barState.drawAttributeBar.colorPickerDarkTone,
				barState.widgetPosition.primaryBar,
				x, y, exact);
			barState.drawAttributeBar.colorPickerMarkerX =
				static_cast<float>(x);
			barState.drawAttributeBar.colorPickerMarkerY =
				static_cast<float>(y);
			// 主栏预设色不是自定义选点；只有非预设当前色或后续色板输入才显示圆环。
			barState.drawAttributeBar.colorPickerMarkerVisible =
				exact && !IsBarPresetColor(currentColor);
		}

	void HandleColorPickerKeyboard(const ExMessage& keyMessage)
		{
			unsigned int keyMask = ColorPickerKeyMask(keyMessage.vkcode);
			if (!keyMask || !ColorPickerAvailable()
				|| !barState.drawAttributeBar.colorPickerOpen)
				return;

			bool keyDown = keyMessage.message == WM_KEYDOWN
				|| keyMessage.message == WM_SYSKEYDOWN;
			unsigned int downMask =
				barState.drawAttributeBar.colorPickerKeyboardDownMask;
			if (!keyDown)
			{
				unsigned int nextMask = downMask & ~keyMask;
				barState.drawAttributeBar.colorPickerKeyboardDownMask = nextMask;
				if ((downMask & keyMask) != 0 && nextMask == 0)
				{
					// 最后一个移动键抬起才持久化，系统重复按下不会写配置。
					SetPenColor(GetPenColor(), true);
					UpdateRendering();
				}
				return;
			}

			auto palette = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette];
			if (!palette || palette->w.val <= 0.0 || palette->h.val <= 0.0)
				return;
			double markerX = barState.drawAttributeBar.colorPickerMarkerX;
			double markerY = barState.drawAttributeBar.colorPickerMarkerY;
			if (!barState.drawAttributeBar.colorPickerMarkerVisible)
			{
				bool exact = false;
				ProjectBarColorPickerColor(GetPenColor() & 0x00FFFFFF,
					barState.drawAttributeBar.colorPickerDarkTone,
					barState.widgetPosition.primaryBar,
					markerX, markerY, exact);
			}
			double horizontalStep = BarColorPickerKeyboardStepDip / palette->w.val;
			double verticalStep = BarColorPickerKeyboardStepDip / palette->h.val;
			switch (keyMessage.vkcode)
			{
			case VK_LEFT: case 'A': markerX -= horizontalStep; break;
			case VK_RIGHT: case 'D': markerX += horizontalStep; break;
			case VK_UP: case 'W': markerY -= verticalStep; break;
			case VK_DOWN: case 'S': markerY += verticalStep; break;
			default: break;
			}
			ApplyColorPickerPoint(markerX, markerY, false);
			barState.drawAttributeBar.colorPickerKeyboardDownMask =
				downMask | keyMask;
			UpdateRendering();
		}

	void RunColorPickerPointerGesture(ExMessage& gestureMessage)
		{
			if (gestureMessage.message != WM_LBUTTONDOWN
				|| !ColorPickerAvailable()
				|| !barState.drawAttributeBar.colorPickerOpen)
				return;
			auto palette = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette];
			if (!palette || palette->w.val <= 0.0 || palette->h.val <= 0.0)
				return;

			COLORREF finalColor = GetPenColor();
			auto ApplyClientPoint = [&](int clientX, int clientY)
				{
					double zoom = max(0.000001,
						static_cast<double>(barStyle.zoom));
					double logicalX = static_cast<double>(clientX) / zoom;
					double logicalY = static_cast<double>(clientY) / zoom;
					barState.drawAttributeBar.colorPickerPointerY =
						static_cast<float>(logicalY);
					double markerX = clamp(
						(logicalX - static_cast<double>(palette->inhX))
							/ palette->w.val, 0.0, 1.0);
					double markerY = clamp(
						(logicalY - static_cast<double>(palette->inhY))
							/ palette->h.val, 0.0, 1.0);
					ApplyColorPickerPoint(markerX, markerY, false);
					finalColor = GetPenColor();
				};

			POINT stablePoint{
				static_cast<LONG>(gestureMessage.x),
				static_cast<LONG>(gestureMessage.y) };
			BarLayoutToScreen(stablePoint);
			POINT activeScreenPoint = stablePoint;
			ULONGLONG stableStartTick = GetTickCount64();
			double stableThreshold = BarColorPickerHoldStillnessPx
				* max(1.0, static_cast<double>(barStyle.dpiZoom));
			auto ResetHold = [&](POINT screenPoint)
				{
					stablePoint = screenPoint;
					stableStartTick = GetTickCount64();
					barState.drawAttributeBar.colorPickerHoldHintActive = false;
					barState.drawAttributeBar.colorPickerHoldProgress = 0.0f;
				};
			auto UpdateHold = [&](POINT screenPoint)
				{
					if (barState.drawAttributeBar.colorPickerHoldLocked) return;
					double dx = static_cast<double>(screenPoint.x - stablePoint.x);
					double dy = static_cast<double>(screenPoint.y - stablePoint.y);
					if (sqrt(dx * dx + dy * dy) > stableThreshold)
					{
						ResetHold(screenPoint);
						UpdateRendering(false);
						return;
					}
					ULONGLONG stillMs = GetTickCount64() - stableStartTick;
					if (stillMs < BarColorPickerHoldHintDelayMs) return;
					barState.drawAttributeBar.colorPickerHoldHintActive = true;
					double progress = clamp(
						static_cast<double>(stillMs - BarColorPickerHoldHintDelayMs)
							/ static_cast<double>(BarColorPickerHoldLockDelayMs),
						0.0, 1.0);
					barState.drawAttributeBar.colorPickerHoldProgress =
						static_cast<float>(progress);
					if (progress >= 1.0)
						barState.drawAttributeBar.colorPickerHoldLocked = true;
					UpdateRendering(false);
				};

			auto& picker = barState.drawAttributeBar;
		picker.colorPickerPointerPressed = true;
		picker.colorPickerKeyboardDownMask = 0;
		picker.colorPickerHoldHintActive = false;
		picker.colorPickerHoldLocked = false;
		picker.colorPickerHoldProgress = 0.0f;
		ApplyClientPoint(gestureMessage.x, gestureMessage.y);
		SendMessage(floating_window, BarColorPickerCaptureMessage,
			BarColorPickerCaptureStart, 0);
		UpdateRendering();

		while (!offSignal)
		{
			if (!TryGetBarInteractionMessage(
				&gestureMessage, EM_MOUSE, true, floating_window))
			{
				if (!ColorPickerAvailable() || !picker.colorPickerOpen
					|| (!Inkeys::Inputs::IsKeyBoardDown(VK_LBUTTON)
						&& !gestureMessage.lbutton))
					break;
				// 触摸不会保证同步系统鼠标位置，静止期间沿用最后一个触点坐标。
				UpdateHold(activeScreenPoint);
				std::this_thread::sleep_for(std::chrono::milliseconds(8));
				continue;
			}
			if (!ColorPickerAvailable() || !picker.colorPickerOpen) break;
			if (gestureMessage.message == WM_MOUSEMOVE && gestureMessage.lbutton)
			{
				POINT screenPoint{
					static_cast<LONG>(gestureMessage.x),
					static_cast<LONG>(gestureMessage.y) };
				BarLayoutToScreen(screenPoint);
				activeScreenPoint = screenPoint;
				if (!picker.colorPickerHoldLocked)
					ApplyClientPoint(gestureMessage.x, gestureMessage.y);
				UpdateHold(screenPoint);
				UpdateRendering(false);
				continue;
			}
			if (gestureMessage.message == WM_LBUTTONUP
				|| !gestureMessage.lbutton)
				break;
		}

		bool captured = picker.colorPickerPointerCapture;
		if (captured)
			SendMessage(floating_window, BarColorPickerCaptureMessage,
				BarColorPickerCaptureStop, 0);
		bool completed = captured && !offSignal
			&& !gestureMessage.lbutton && ColorPickerAvailable()
			&& picker.colorPickerOpen;
		picker.colorPickerPointerPressed = false;
		picker.colorPickerHoldHintActive = false;
		picker.colorPickerHoldLocked = false;
		picker.colorPickerHoldProgress = 0.0f;
		if (completed)
			SetPenColor(Inkeys::Color::SetAlphaR(finalColor, 255), true);
		UpdateRendering();
		SuppressHoverUntilPointerMove();
		ClearBarInteractionMessages(EM_MOUSE, floating_window);
	}

	BarInteractionStageResult FinishInteraction()
		{
			CloseThicknessSlider(false);
			CloseColorPicker(false);
			ClosePenTypeMenu();
			return BarInteractionStageResult::Shutdown;
		}

	BarInteractionStageResult PollInteractionMessage()
		{
		currentTouchScreenSample.ready = false;
		if ((thicknessFineDialPhase == ThicknessFineDialPhase::Inertia
			|| thicknessFineDialPhase == ThicknessFineDialPhase::Settling)
			&& !barState.drawAttributeBar.thicknessFineDialPhysicsActive)
		{
			// 生命周期线程已撤销共享状态时，同步丢弃交互线程残余速度。
			thicknessFineDialPhase = ThicknessFineDialPhase::Idle;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialResidualVelocity = 0.0;
			ResetThicknessFineDialSamples();
		}
		bool thicknessPhysicsPolling =
			barState.drawAttributeBar.thicknessFineDialPhysicsActive
			&& (thicknessFineDialPhase == ThicknessFineDialPhase::Inertia
				|| thicknessFineDialPhase
					== ThicknessFineDialPhase::Settling);
		if (thicknessPhysicsPolling)
		{
			if (!TryGetBarInteractionMessage(
				&msg, EM_MOUSE | EM_KEY, true, floating_window,
				&currentTouchScreenSample))
			{
				AdvanceThicknessFineDialPhysics();
				std::this_thread::sleep_for(
					BarThicknessFineDialPhysicsPollInterval);
				return BarInteractionStageResult::Consumed;
			}
			// 任意消息或嵌套按压都会暂停物理；下一次空轮询从零 dt 接续。
			thicknessFineDialPhysicsClockNeedsReset = true;
		}
		else if (!WaitForBarInteractionMessage(
			msg, EM_MOUSE | EM_KEY, floating_window, false,
			&currentTouchScreenSample))
			return BarInteractionStageResult::Shutdown;
		return BarInteractionStageResult::PassThrough;
		}

	BarInteractionStageResult HandleKeyboardMessage()
		{
		if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP
			|| msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP)
		{
			HandleColorPickerKeyboard(msg);
			return BarInteractionStageResult::Consumed;
		}
		return BarInteractionStageResult::PassThrough;
		}

	BarInteractionStageResult HandleCommonHoverAndOcclusion()
	{
		if (!ColorPickerAvailable()
			&& (barState.drawAttributeBar.colorPickerOpen
				|| barState.drawAttributeBar.colorPickerPointerPressed
				|| barState.drawAttributeBar.colorPickerPointerCapture))
		{
			CloseColorPicker(true);
			UpdateRendering(false);
		}
		if (!ThicknessSliderAvailable()
			&& (barState.drawAttributeBar.thicknessSliderHover
				|| barState.drawAttributeBar.thicknessSliderPinned
				|| barState.drawAttributeBar.thicknessSliderPressed
				|| barState.drawAttributeBar.thicknessSliderDragging
				|| barState.drawAttributeBar.thicknessFineDialDragging
				|| barState.drawAttributeBar.thicknessFineDialPhysicsActive
				|| barState.drawAttributeBar.thicknessViewMode
					!= ThicknessViewMode::Preview
				|| barState.drawAttributeBar.thicknessSliderCapture))
		{
			CloseThicknessSlider(true);
			UpdateRendering(false);
		}
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
					return BarInteractionStageResult::Consumed;
				}
				suppressHoverUntilPointerMove = false;
			}

// 颜色选择器盖住绘制属性时，下方滑块/提示/按钮都不得再被悬停激活。
				bool colorPickerOccludes =
					IsColorPickerOccludingPoint(msg.x, msg.y);
				bool penTypeMenuOccludes =
					IsPenTypeMenuOccludingPoint(msg.x, msg.y);
				auto annotationInfoHit = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit];
				auto overflowInfoHit = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit];
				bool annotationBadgeHover = !colorPickerOccludes
					&& AnnotationTooltipAvailable()
					&& annotationInfoHit
					&& annotationInfoHit->IsClick(
						msg.x, msg.y, barStyle.zoom);
				bool overflowBadgeHover = !colorPickerOccludes
					&& !penTypeMenuOccludes
					&& OverflowTooltipAvailable()
					&& overflowInfoHit
					&& overflowInfoHit->IsClick(
						msg.x, msg.y, barStyle.zoom);
				auto annotationPopup = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup];
				auto overflowPopup = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup];
				bool annotationPopupInteractive =
					barState.drawAttributeBar.thicknessAnnotationHover
					|| barState.drawAttributeBar.thicknessAnnotationHoverGrace
					|| barState.drawAttributeBar.thicknessAnnotationPinned;
				bool overflowPopupInteractive =
					barState.drawAttributeBar.thicknessOverflowHover
					|| barState.drawAttributeBar.thicknessOverflowHoverGrace
					|| barState.drawAttributeBar.thicknessOverflowPinned;
				bool annotationPopupHover = !colorPickerOccludes
					&& AnnotationTooltipAvailable()
					&& annotationPopupInteractive && annotationPopup
					&& annotationPopup->IsClick(
						msg.x, msg.y, barStyle.zoom);
				bool overflowPopupHover = !colorPickerOccludes
					&& !penTypeMenuOccludes
					&& !annotationPopupHover
					&& OverflowTooltipAvailable()
					&& overflowPopupInteractive && overflowPopup
					&& overflowPopup->IsClick(
						msg.x, msg.y, barStyle.zoom);
				bool tooltipHoverChanged = false;
				tooltipHoverChanged |= UpdateTooltipHover(
					AnnotationTooltipAvailable(),
					annotationBadgeHover || annotationPopupHover,
					barState.drawAttributeBar.thicknessAnnotationHover,
					barState.drawAttributeBar.thicknessAnnotationPinned,
					barState.drawAttributeBar.thicknessAnnotationHoverGrace,
					BarThicknessAnnotationTooltipGraceTimerId);
				tooltipHoverChanged |= UpdateTooltipHover(
					OverflowTooltipAvailable(),
					overflowBadgeHover || overflowPopupHover,
					barState.drawAttributeBar.thicknessOverflowHover,
					barState.drawAttributeBar.thicknessOverflowPinned,
					barState.drawAttributeBar.thicknessOverflowHoverGrace,
					BarThicknessOverflowTooltipGraceTimerId);

				auto sliderHit = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderHit];
				// Overflow 属于 Preview；停在其按钮/浮窗时不能反向触发 Slider。
				bool overflowUiActive = overflowBadgeHover
					|| overflowPopupHover
					|| barState.drawAttributeBar.thicknessOverflowHover
					|| barState.drawAttributeBar.thicknessOverflowHoverGrace
					|| barState.drawAttributeBar.thicknessOverflowPinned;
				bool sliderHover = !colorPickerOccludes
					&& !penTypeMenuOccludes
					&& ThicknessSliderAvailable()
					&& barState.drawAttributeBar.thicknessViewMode
						!= ThicknessViewMode::FineDial
					&& !overflowUiActive
					&& ((sliderHit && sliderHit->IsClick(
						msg.x, msg.y, barStyle.zoom))
						|| IsBarThicknessPreviewPopupHit(
							barUISet, msg.x, msg.y)
						|| IsBarThicknessPrecisionDragHit(
							barUISet, msg.x, msg.y));
				bool sliderHoverChanged = static_cast<bool>(
					barState.drawAttributeBar.thicknessSliderHover)
					!= sliderHover;
				if (sliderHoverChanged)
				{
					barState.drawAttributeBar.thicknessSliderHover = sliderHover;
					if (sliderHover
						&& barState.drawAttributeBar.thicknessViewMode
							== ThicknessViewMode::Preview)
						barState.drawAttributeBar.thicknessViewMode =
							ThicknessViewMode::Slider;
					else if (!sliderHover
						&& barState.drawAttributeBar.thicknessViewMode
							== ThicknessViewMode::Slider
						&& !barState.drawAttributeBar.thicknessSliderPinned
						&& !barState.drawAttributeBar.thicknessSliderPressed
						&& !barState.drawAttributeBar.thicknessSliderDragging)
						barState.drawAttributeBar.thicknessViewMode =
							ThicknessViewMode::Preview;
				}
				if (tooltipHoverChanged || sliderHoverChanged)
					UpdateRendering(false);

				BarButtonClass* currentHoveredButton = nullptr;
				const int mainBodyHitTestY =
					barUISet.BottomDockBodyHitTestYFromRigid(msg.y);
				if (!barState.fold && !colorPickerOccludes
					&& !penTypeMenuOccludes)
				{
					for (int id = 0; id < barButtonSet.tot; id++)
					{
						BarButtonClass* temp = barButtonSet.buttonList.Get(id);
						if (!temp || !temp->IsVisible()
							|| temp->preset == BarButtonPresetEnum::Divider
							|| temp->state->state == BarWidgetState::Selected) continue;
						bool isColorSelector = temp->name.enable.tar
							&& temp->name.content.GetTar().substr(0, 7) == L"__color";
						if (isColorSelector) continue; // 颜色块自身就是内容，不把其填充色改成悬停灰色。
						if (temp->button.IsClick(
							msg.x, mainBodyHitTestY, barStyle.zoom))
						{
							currentHoveredButton = temp;
							break;
						}
					}
					if (!currentHoveredButton && barState.moreExpanded)
					{
						BarMoreButtonSnapshotClass hoverSnapshot =
							barButtonSet.GetMoreButtonSnapshot();
						auto FindHoveredMoreButton =
							[&](const vector<shared_ptr<BarButtonClass>>& buttons)
							{
								for (const shared_ptr<BarButtonClass>& button : buttons)
								{
									if (!button || !button->IsVisible()
										|| button->state->state == BarWidgetState::Selected)
										continue;
									if (button->button.IsClick(
										msg.x, msg.y, barStyle.zoom))
									{
										currentHoveredButton = button.get();
										break;
									}
								}
							};
						FindHoveredMoreButton(hoverSnapshot.explicitMore);
						if (!currentHoveredButton)
							FindHoveredMoreButton(hoverSnapshot.forcedOverflow);
					}
				}
				if (currentHoveredButton != hoveredMainBarButton)
				{
					StopMainBarButtonHover(hoveredMainBarButton, false);
					hoveredMainBarButton = currentHoveredButton;
					StartMainBarButtonHover(hoveredMainBarButton);
				}

				IndependentHoverTargetEnum currentIndependentButton = IndependentHoverTargetEnum::None;
				if (!colorPickerOccludes && IsIndependentHoverAllowed(
					IndependentHoverTargetEnum::DrawAttributePenTypeFreeLine))
				{
					auto freeLine = shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine];
					if (freeLine && freeLine->IsClick(
						msg.x, msg.y, barStyle.zoom))
						currentIndependentButton = IndependentHoverTargetEnum::
							DrawAttributePenTypeFreeLine;
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None
					&& !colorPickerOccludes && !penTypeMenuOccludes
					&& IsIndependentHoverAllowed(
						IndependentHoverTargetEnum::DrawAttributePenTypeExtension))
				{
					auto extension = shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
					if (extension && extension->IsClick(
						msg.x, msg.y, barStyle.zoom))
						currentIndependentButton = IndependentHoverTargetEnum::
							DrawAttributePenTypeExtension;
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None
					&& !penTypeMenuOccludes
					&& IsIndependentHoverAllowed(IndependentHoverTargetEnum::MoreClose))
				{
					auto moreClose = shapeMap[BarUISetShapeEnum::MorePanelCloseHit];
					if (moreClose && moreClose->IsClick(
						msg.x, msg.y, barStyle.zoom))
						currentIndependentButton = IndependentHoverTargetEnum::MoreClose;
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None
					&& ColorPickerAvailable() && barState.drawAttributeBar.colorPickerOpen)
				{
					auto colorPickerCloseHit = shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit];
					auto colorPickerToneHit = shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle];
					if (IsIndependentHoverAllowed(
						IndependentHoverTargetEnum::DrawAttributeColorPickerClose)
						&& colorPickerCloseHit
						&& colorPickerCloseHit->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeColorPickerClose;
					}
					else if (IsIndependentHoverAllowed(
						IndependentHoverTargetEnum::DrawAttributeColorPickerTone)
						&& colorPickerToneHit
						&& colorPickerToneHit->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeColorPickerTone;
					}
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None
					&& !penTypeMenuOccludes
					&& barState.geometryAttribute && !barState.fold)
				{
					const BarUISetShapeEnum geometryShapes[] =
					{
						BarUISetShapeEnum::GeometryAttributeBar_StraightLine,
						BarUISetShapeEnum::GeometryAttributeBar_Rectangle,
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine,
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium,
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse,
						BarUISetShapeEnum::GeometryAttributeBar_Close,
					};
					for (size_t index = 0; index < size(geometryShapes); ++index)
					{
						auto target = static_cast<IndependentHoverTargetEnum>(
							static_cast<int>(IndependentHoverTargetEnum::GeometryStraightLine)
							+ static_cast<int>(index));
						auto shape = shapeMap[geometryShapes[index]];
						if (IsIndependentHoverAllowed(target) && shape
							&& shape->IsClick(msg.x, msg.y, barStyle.zoom))
						{
							currentIndependentButton = target;
							break;
						}
					}
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None
					&& barState.drawAttribute && !colorPickerOccludes
					&& !penTypeMenuOccludes)
				{
				// 两个浮窗允许覆盖，按绘制顺序优先命中上层的标注帮助浮窗。
				auto overflowClose = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit];
				auto annotationClose = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit];
				if (IsIndependentHoverAllowed(
					IndependentHoverTargetEnum::DrawAttributeAnnotationClose)
					&& annotationClose
					&& annotationClose->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					currentIndependentButton =
						IndependentHoverTargetEnum::DrawAttributeAnnotationClose;
				}
				else if (IsIndependentHoverAllowed(
					IndependentHoverTargetEnum::DrawAttributeOverflowClose)
					&& overflowClose
					&& overflowClose->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					currentIndependentButton =
						IndependentHoverTargetEnum::DrawAttributeOverflowClose;
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
		return BarInteractionStageResult::PassThrough;
	}

	BarInteractionStageResult HandleColorPickerPointerStage()
	{
			bool continueFlag = true;

			// 面板外按下先收起，再把同一次点击继续交给绘制属性或主栏控件。
			if (continueFlag && msg.message == WM_LBUTTONDOWN
				&& ColorPickerAvailable()
				&& barState.drawAttributeBar.colorPickerOpen)
			{
				auto pickerPanel = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
				auto customSwatch = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
				bool insidePicker = pickerPanel && pickerPanel->IsClick(
					msg.x, msg.y, barStyle.zoom);
				bool insideEntry = customSwatch && customSwatch->IsClick(
					msg.x, msg.y, barStyle.zoom);
				if (!insidePicker && !insideEntry)
				{
					CloseColorPicker(false);
					UpdateRendering(false);
				}
			}

			// 颜色面板位于最上层；面板内部阻止点击穿透，入口和 X 可主动关闭。
			if (continueFlag && ColorPickerAvailable()
				&& barState.drawAttributeBar.colorPickerOpen)
			{
				auto pickerPanel = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
				auto pickerPalette = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette];
				auto pickerTone = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle];
				auto pickerClose = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit];
				if (pickerClose && pickerClose->IsClick(
					msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						StopIndependentHover(
							hoveredIndependentButton, true, true);
						hoveredIndependentButton =
							IndependentHoverTargetEnum::None;
						barState.drawAttributeBar.colorPickerClosePress = true;
						UpdateRendering(false);
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!pickerClose->IsClick(
								msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
								CloseColorPicker(false);
								break;
							}
						}
						barState.drawAttributeBar.colorPickerClosePress = false;
						UpdateRendering(false);
						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(
							EM_MOUSE, floating_window);
					}
				}
				else if (pickerTone && pickerTone->IsClick(
					msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						StopIndependentHover(
							hoveredIndependentButton, true, true);
						hoveredIndependentButton =
							IndependentHoverTargetEnum::None;
						barState.drawAttributeBar.colorPickerTonePress = true;
						UpdateRendering(false);
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!pickerTone->IsClick(
								msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
								barState.drawAttributeBar.colorPickerDarkTone =
									!static_cast<bool>(barState.drawAttributeBar
										.colorPickerDarkTone);
								// 切换只反投影选点，不写画笔；不可精确表示时隐藏标记。
								ProjectCurrentColorPickerPoint();
								break;
							}
						}
						barState.drawAttributeBar.colorPickerTonePress = false;
						UpdateRendering(false);
						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(
							EM_MOUSE, floating_window);
					}
				}
				else if (pickerPalette && pickerPalette->IsClick(
					msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					RunColorPickerPointerGesture(msg);
				}
				else if (pickerPanel && pickerPanel->IsClick(
					msg.x, msg.y, barStyle.zoom))
				{
					// 面板正文阻止点击穿透；面板外则维持既有主栏交互。
					continueFlag = false;
				}
			}

			if (continueFlag && ColorPickerAvailable())
			{
				auto customSwatch = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
				if (customSwatch && customSwatch->IsClick(
					msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						BarColorPickerEntryPressed = true;
						UpdateRendering(false);
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!customSwatch->IsClick(
								msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
								if (barState.drawAttributeBar.colorPickerOpen)
									CloseColorPicker(false);
								else
								{
									ClosePenTypeMenu();
									CloseDrawAttributeTooltips();
									CloseThicknessSlider(true);
									barState.drawAttributeBar.colorPickerOpen = true;
									ProjectCurrentColorPickerPoint();
								}
								UpdateRendering(false);
								break;
							}
						}
						BarColorPickerEntryPressed = false;
						UpdateRendering(false);
						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(
							EM_MOUSE, floating_window);
					}
				}
			}
			return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult HandleOverlayPointerStage()
	{
			bool continueFlag = true;

			// 提示控件位于顶层，固定浮窗必须先于下方主栏和属性栏消费点击。
			struct ThicknessTooltipInteraction
			{
				BarUISetShapeEnum infoHit;
				BarUISetShapeEnum popup;
				BarUISetShapeEnum closeHit;
				IdtAtomic<bool>* hover;
				IdtAtomic<bool>* hoverGrace;
				IdtAtomic<bool>* pinned;
				IdtAtomic<bool>* pressed;
				UINT_PTR hoverGraceTimerId;
				bool available;
			};
			ThicknessTooltipInteraction tooltipInteractions[] =
			{
				{
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
					&barState.drawAttributeBar.thicknessAnnotationHover,
					&barState.drawAttributeBar.thicknessAnnotationHoverGrace,
					&barState.drawAttributeBar.thicknessAnnotationPinned,
					&barState.drawAttributeBar.thicknessAnnotationClosePress,
					BarThicknessAnnotationTooltipGraceTimerId,
					AnnotationTooltipAvailable(),
				},
				{
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
					BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
					&barState.drawAttributeBar.thicknessOverflowHover,
					&barState.drawAttributeBar.thicknessOverflowHoverGrace,
					&barState.drawAttributeBar.thicknessOverflowPinned,
					&barState.drawAttributeBar.thicknessOverflowClosePress,
					BarThicknessOverflowTooltipGraceTimerId,
					OverflowTooltipAvailable(),
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
					CancelTooltipHoverGrace(
						*tooltip.hoverGrace, tooltip.hoverGraceTimerId);
					*tooltip.pressed = true;
					StopIndependentHover(
						hoveredIndependentButton, true, true);
					hoveredIndependentButton =
						IndependentHoverTargetEnum::None;
					UpdateRendering(false);
					while (true)
					{
						if (!WaitForBarInteractionMessage(
							msg, EM_MOUSE, floating_window))
						{
							return BarInteractionStageResult::Shutdown;
						}
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
					ClearBarInteractionMessages(EM_MOUSE, floating_window);
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
					CancelTooltipHoverGrace(
						*tooltip.hoverGrace, tooltip.hoverGraceTimerId);
					while (true)
					{
						if (!WaitForBarInteractionMessage(
							msg, EM_MOUSE, floating_window))
						{
							return BarInteractionStageResult::Shutdown;
						}
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
					ClearBarInteractionMessages(EM_MOUSE, floating_window);
				}
			}

			for (auto& tooltip : tooltipInteractions)
			{
				auto popup = shapeMap[tooltip.popup];
				if (continueFlag && tooltip.available
					&& (static_cast<bool>(*tooltip.pinned)
						|| static_cast<bool>(*tooltip.hover)) && popup
					&& popup->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					// 悬停或固定浮窗正文都阻止点击穿透，关闭动作仍由 X 独立处理。
					continueFlag = false;
				}
			}

			// 菜单外点击先退场，但不吞掉同一条消息，让下层控件继续处理。
			if (continueFlag && msg.message == WM_LBUTTONDOWN
				&& barState.drawAttributeBar.penTypeMenuOpen)
			{
				auto menu = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu];
				auto entry = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
				bool insideMenu = menu && menu->IsClick(
					msg.x, msg.y, barStyle.zoom);
				bool insideEntry = entry && entry->IsClick(
					msg.x, msg.y, barStyle.zoom);
				if (!insideMenu && !insideEntry)
				{
					ClosePenTypeMenu();
					UpdateRendering(false);
				}
			}

			if (continueFlag && barState.drawAttributeBar.penTypeMenuOpen)
			{
				auto menu = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu];
				auto freeLine = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine];
				auto annotationLine = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuAnnotationLine];
				bool insideFreeLine = freeLine && freeLine->IsClick(
					msg.x, msg.y, barStyle.zoom);
				bool insideAnnotationLine = annotationLine && annotationLine->IsClick(
					msg.x, msg.y, barStyle.zoom);
				bool insideMenu = menu && menu->IsClick(
					msg.x, msg.y, barStyle.zoom);
				if (insideFreeLine)
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.drawAttributeBar.penTypeFreeLinePress = true;
						StopIndependentHover(
							hoveredIndependentButton, true, true);
						hoveredIndependentButton =
							IndependentHoverTargetEnum::None;
						UpdateRendering(false);
						bool clickCompleted = false;
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!freeLine->IsClick(
								msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
								clickCompleted = true;
								ClosePenTypeMenu();
								break;
							}
						}
						barState.drawAttributeBar.penTypeFreeLinePress = false;
						UpdateRendering(false);
						if (clickCompleted) SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(
							EM_MOUSE, floating_window);
					}
				}
				else if (insideAnnotationLine || insideMenu)
				{
					// 标注线当前禁用；菜单正文和空白区域均阻止穿透。
					continueFlag = false;
				}
			}
			return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult HandleMorePointerStage()
	{
			bool continueFlag = true;

			// 更多浮层视觉位于主栏下层，但展开后命中优先于主栏；外部点击关闭后继续处理原点击。
			if (continueFlag && barState.moreExpanded)
			{
				auto morePanel = shapeMap[BarUISetShapeEnum::MorePanel];
				auto moreClose = shapeMap[BarUISetShapeEnum::MorePanelCloseHit];
				BarButtonClass* moreButton = barButtonSet.GetMoreButton();
				BarMoreButtonSnapshotClass moreSnapshot =
					barButtonSet.GetMoreButtonSnapshot();
				bool pointerInPanel = morePanel
					&& morePanel->IsClick(msg.x, msg.y, barStyle.zoom);
				bool pointerInMoreButton = moreButton
					&& moreButton->button.IsClick(msg.x, msg.y, barStyle.zoom);

				if (moreClose && moreClose->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.moreClosePress = true;
						StopIndependentHover(hoveredIndependentButton, true, true);
						hoveredIndependentButton = IndependentHoverTargetEnum::None;
						UpdateRendering(false);
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!moreClose->IsClick(msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
								barState.moreExpanded = false;
								break;
							}
						}
						barState.moreClosePress = false;
						UpdateRendering(false);
						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(EM_MOUSE, floating_window);
					}
				}
				else
				{
					vector<shared_ptr<BarButtonClass>> moreButtons;
					moreButtons.reserve(moreSnapshot.explicitMore.size()
						+ moreSnapshot.forcedOverflow.size());
					moreButtons.insert(moreButtons.end(),
						moreSnapshot.explicitMore.begin(), moreSnapshot.explicitMore.end());
					moreButtons.insert(moreButtons.end(),
						moreSnapshot.forcedOverflow.begin(), moreSnapshot.forcedOverflow.end());
					for (const shared_ptr<BarButtonClass>& button : moreButtons)
					{
						if (!button || !button->IsVisible()
							|| !button->button.IsClick(msg.x, msg.y, barStyle.zoom)) continue;
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN)
						{
							button->state->emph = BarWidgetEmphasize::Pressed;
							StopMainBarButtonHover(hoveredMainBarButton, true, true);
							hoveredMainBarButton = nullptr;
							UpdateRendering(false);
							while (true)
							{
								if (!WaitForBarInteractionMessage(
									msg, EM_MOUSE, floating_window))
								{
									return BarInteractionStageResult::Shutdown;
								}
								if (!button->button.IsClick(msg.x, msg.y, barStyle.zoom)) break;
								if (!msg.lbutton)
								{
									// 默认先收起再执行，避免动作打开新窗口时浮层残留。
									if (button->closeMoreAfterAction)
										barState.moreExpanded = false;
									if (button->clickFunc) button->clickFunc();
									break;
								}
							}
							button->state->emph = BarWidgetEmphasize::None;
							UpdateRendering(false);
							SuppressHoverUntilPointerMove();
							ClearBarInteractionMessages(EM_MOUSE, floating_window);
						}
						break;
					}
				}

				if (continueFlag && pointerInPanel)
					continueFlag = false;
				else if (continueFlag && !pointerInMoreButton
					&& (msg.message == WM_LBUTTONDOWN
						|| msg.message == WM_RBUTTONDOWN))
				{
					barState.moreExpanded = false;
					UpdateRendering(false);
				}
			}
			return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult HandleMainButtonAndBarPointerStage()
	{
			bool continueFlag = true;
			const short rigidMessageY = msg.y;
			int visualMessageY = rigidMessageY;
			ApplyBarBottomDockBodyHitTestFromRigid(msg, &visualMessageY);

			// 主按钮
			if (auto obj = superellipseMap[BarUISetSuperellipseEnum::MainButton]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
			{
				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					// Seek 需要按下时的真实视觉坐标，不能把主体逆形变坐标当作抓取点。
					ExMessage seekMessage = msg;
					seekMessage.y = static_cast<short>(clamp(
						visualMessageY, static_cast<int>(SHRT_MIN),
						static_cast<int>(SHRT_MAX)));
					const BarSeekResult seekResult = Seek(seekMessage);
					if (seekResult.allowClick)
					{
						if (barUISet.TryBeginToggle(BarToggleChannel::Main))
						{
							mainButtonClickPulseSerial.fetch_add(
								1, std::memory_order_relaxed);
							// 展开/收起主栏
							if (barState.fold) barState.fold = false;
							else
							{
								barState.fold = true;
								Inkeys::UI::Bar::ClearWhiteboardDockLock();
								barState.moreExpanded = false;
								CloseThicknessSlider(true);
								CloseColorPicker(true);
							}
							UpdateRendering();
						}
					}
					SuppressHoverUntilPointerMove();

					ClearBarInteractionMessages(EM_MOUSE, floating_window);
				}
				if (msg.message == WM_RBUTTONDOWN && setlist.RightClickClose)
				{
					if (MessageBox(floating_window, L"Whether to turn off 智绘教Inkeys?\n是否关闭 智绘教Inkeys？", L"Inkeys Tips | 智绘教提示", MB_OKCANCEL | MB_SYSTEMMODAL) == 1) CloseProgram();

					ClearBarInteractionMessages(EM_MOUSE, floating_window);
				}
			}

			// 按钮
			if (continueFlag)
			{
				// 特殊体质：按钮
				for (int id = 0; id < barButtonSet.tot; id++)
				{
					BarButtonClass* temp = barButtonSet.buttonList.Get(id);
					if (temp == nullptr || !temp->IsVisible()
						|| temp->preset == BarButtonPresetEnum::Divider) continue;

					// 双击第二击仍归属于第一击按钮，避免动画中按钮位移导致命中丢失。
					bool doubleClickContinuation = msg.message == WM_LBUTTONDBLCLK
						&& temp == lastClickedMainBarButton;
					if (temp->button.IsClick(msg.x, msg.y, barStyle.zoom) || doubleClickContinuation)
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
								if (!WaitForBarInteractionMessage(
									msg, EM_MOUSE, floating_window))
								{
									return BarInteractionStageResult::Shutdown;
								}
								ApplyBarBottomDockBodyHitTestFromRigid(msg);
								if (doubleClickContinuation || temp->button.IsClick(msg.x, msg.y, barStyle.zoom))
								{
									// Move 缺少 MK_LBUTTON 不能代表抬起，点击只在明确 Up 后执行。
									if (msg.message == WM_LBUTTONUP && !msg.lbutton)
									{
										ClosePenTypeMenu();
										if (temp->preset == BarButtonPresetEnum::More)
										{
											if (barUISet.TryBeginToggle(
												BarToggleChannel::More))
											{
												bool opening = !static_cast<bool>(
													barState.moreExpanded);
												barState.moreExpanded = opening;
												if (opening)
												{
													CloseDrawAttributeTooltips();
													barState.drawAttribute = false;
													barState.geometryAttribute = false;
													CloseThicknessSlider(true);
													CloseColorPicker(true);
												}
											}
										}
										else if (temp->clickFunc) temp->clickFunc();
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
							if (!clickCompleted) ClearBarInteractionMessages(EM_MOUSE, floating_window);
						}
						break;
					}
				}
			}
			msg.y = rigidMessageY;
			return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult HandleGeometryPointerStage()
	{
			bool continueFlag = true;

			// 几何属性按钮：按下即缩小，拖出取消，抬起后等待新的指针移动再恢复悬停。
			if (continueFlag && barState.geometryAttribute
				&& stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
			{
				struct GeometryButtonInteraction
				{
					BarUISetShapeEnum shape;
					IdtAtomic<bool>* pressed;
					optional<ShapeModeSelectEnum> shapeMode;
					int thicknessPresetIndex;
					bool closePanel;
				};
				const GeometryButtonInteraction geometryButtons[] =
				{
					{ BarUISetShapeEnum::GeometryAttributeBar_StraightLine,
						&barState.geometryAttributeBar.straightLinePress,
						ShapeModeSelectEnum::IdtShapeStraightLine1, -1, false },
					{ BarUISetShapeEnum::GeometryAttributeBar_Rectangle,
						&barState.geometryAttributeBar.rectanglePress,
						ShapeModeSelectEnum::IdtShapeRectangle1, -1, false },
					{ BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine,
						&barState.geometryAttributeBar.thicknessFinePress,
						nullopt, 0, false },
					{ BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium,
						&barState.geometryAttributeBar.thicknessMediumPress,
						nullopt, 1, false },
					{ BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse,
						&barState.geometryAttributeBar.thicknessCoarsePress,
						nullopt, 2, false },
					{ BarUISetShapeEnum::GeometryAttributeBar_Close,
						&barState.geometryAttributeBar.closePress,
						nullopt, -1, true },
				};
				for (const auto& button : geometryButtons)
				{
					auto shape = shapeMap[button.shape];
					if (!shape || !shape->IsClick(msg.x, msg.y, barStyle.zoom))
						continue;

					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						*button.pressed = true;
						StopIndependentHover(hoveredIndependentButton, true, true);
						hoveredIndependentButton = IndependentHoverTargetEnum::None;
						UpdateRendering(false);
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!shape->IsClick(msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
								if (button.closePanel)
									barState.geometryAttribute = false;
								else if (button.shapeMode.has_value())
								{
									stateMode.Shape.ModeSelect = button.shapeMode.value();
									SyncDraw3State();
								}
								else SetPenWidth(static_cast<float>(
									GetBarThicknessPresetPx(
										PenModeSelectEnum::IdtPenBrush1,
										button.thicknessPresetIndex,
										barStyle.dpiZoom)));
								UpdateRendering();
								break;
							}
						}
						*button.pressed = false;
						UpdateRendering(false);
						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(EM_MOUSE, floating_window);
					}
					break;
				}
			}
			return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult HandleThicknessAndFineDialPointerStage()
	{
			bool continueFlag = true;

			// 绘制属性
				{
					// 鼠标沿用 Slider；未悬停的直接触摸先判定点击或 Preview 拖动。
					if (continueFlag && ThicknessSliderAvailable())
					{
						auto sliderHit = shapeMap[
							BarUISetShapeEnum::
								DrawAttributeBar_ThicknessSliderHit];
						bool previewPopupHit =
							IsBarThicknessPreviewPopupHit(
								barUISet, msg.x, msg.y);
						BarThicknessFineDialHitZone fineActivationHit =
							HitTestBarThicknessFineDialFreshActivation(
								barUISet, msg.x, msg.y);
						bool precisionDragHit = fineActivationHit
							== BarThicknessFineDialHitZone::Drag;
						bool fineActivationCorridorConsumed = !previewPopupHit
							&& fineActivationHit
								== BarThicknessFineDialHitZone::Consumed;
						bool rangeTransitionConsumesPress =
							barState.drawAttributeBar.thicknessViewMode
								== ThicknessViewMode::FineDial
							&& barState.drawAttributeBar
								.thicknessFineDialRangeTransitionActive;
						if ((sliderHit && sliderHit->IsClick(
							msg.x, msg.y, barStyle.zoom))
							|| previewPopupHit
							|| fineActivationHit
								!= BarThicknessFineDialHitZone::None)
						{
							continueFlag = false;
							if (msg.message == WM_LBUTTONDOWN
								&& !fineActivationCorridorConsumed
								&& !rangeTransitionConsumesPress)
							{
								PenModeSelectEnum gesturePenMode =
									stateMode.Pen.ModeSelect;
								auto range = GetBarThicknessSliderRange(
									gesturePenMode, barStyle.dpiZoom);
								float initialWidth = GetPenWidth();
								float finalWidth = initialWidth;
								int lastCandidateWidth =
									static_cast<int>(lround(initialWidth));
								bool candidateWidthIsInteger =
									abs(static_cast<double>(initialWidth)
										- lastCandidateWidth) <= 0.000001;
								bool candidateChanged = false;
								bool gestureDragged = false;
								bool hoverAtPress = barState.drawAttributeBar
									.thicknessSliderHover;
								bool pinnedAtPress = barState.drawAttributeBar
									.thicknessSliderPinned;
								ThicknessViewMode viewModeAtPress =
									barState.drawAttributeBar.thicknessViewMode;
								bool previewPopupGesture = previewPopupHit;
								bool sliderPopupGesture = previewPopupGesture
									&& viewModeAtPress == ThicknessViewMode::Slider;
								bool fineDialPopupReturnGesture = previewPopupGesture
									&& viewModeAtPress == ThicknessViewMode::FineDial;
								bool pointerUsesTouchCoordinates =
									IsBarTouchPointerMessage(msg);
								// 悬停/固定只表示进入滑块态；改值还要等圆点完全出现。
								bool sliderAlreadyShown =
									viewModeAtPress != ThicknessViewMode::Preview;
								bool directTouchPreviewGesture =
									pointerUsesTouchCoordinates
									&& viewModeAtPress == ThicknessViewMode::Preview;
								bool fineDragActivationArmed = precisionDragHit;
								bool fineDialGesture =
									viewModeAtPress == ThicknessViewMode::FineDial
									&& !fineDialPopupReturnGesture;
								bool precisionRelativeGesture = false;
								bool touchGestureCancelled = false;
								bool penModeChanged = false;
								if (fineDialPopupReturnGesture
									&& barState.drawAttributeBar
										.thicknessFineDialCandidateActive)
								{
									double visualWidth = static_cast<double>(
										barState.drawAttributeBar
											.thicknessFineDialVisualWidth);
									if (isfinite(visualWidth))
									{
										initialWidth = static_cast<float>(clamp(
											visualWidth,
											static_cast<double>(range.min),
											static_cast<double>(range.max)));
										finalWidth = initialWidth;
										lastCandidateWidth = clamp(
											static_cast<int>(lround(initialWidth)),
											range.min, range.max);
										candidateWidthIsInteger = abs(
											static_cast<double>(initialWidth)
												- lastCandidateWidth) <= 0.000001;
									}
								}

								POINT startScreenPoint{
									static_cast<LONG>(msg.x),
									static_cast<LONG>(msg.y) };
								BarLayoutToScreen(startScreenPoint);
								double pressScreenX =
									static_cast<double>(
										startScreenPoint.x);
								double pressScreenY =
									static_cast<double>(
										startScreenPoint.y);
								double touchDragThresholdPx =
									BarThicknessPreviewTouchSlopDip
									* max(1.0, static_cast<double>(
										barStyle.dpiZoom));
								double touchDragThresholdSquared =
									touchDragThresholdPx * touchDragThresholdPx;

								auto panel = shapeMap[
									BarUISetShapeEnum::DrawAttributeBar];
								auto region = shapeMap[
									BarUISetShapeEnum::
										DrawAttributeBar_ThicknessSelect];
								auto adjust = shapeMap[
									BarUISetShapeEnum::
										DrawAttributeBar_ThicknessAdjust];
								auto sliderThumb = shapeMap[
									BarUISetShapeEnum::
										DrawAttributeBar_ThicknessSliderThumb];
								auto previewGeometry =
									CalculateBarThicknessPreviewGeometry(
										*panel, *region,
										BarUiInheritClass(
											region->inhX, region->inhY),
										*adjust,
										BarUiInheritClass(
											adjust->inhX, adjust->inhY));
								double halfThumb =
									BarThicknessSliderThumbDiameter
									* previewGeometry.panelScale / 2.0;
								POINT trackStartPoint{
									static_cast<LONG>(lround(
										(previewGeometry.trackLeft + halfThumb)
										* static_cast<double>(barStyle.zoom))), 0 };
								POINT trackEndPoint{
									static_cast<LONG>(lround(
										(previewGeometry.trackRight - halfThumb)
										* static_cast<double>(barStyle.zoom))), 0 };
								BarLayoutToScreen(trackStartPoint);
								BarLayoutToScreen(trackEndPoint);
								double trackStartScreenX =
									static_cast<double>(trackStartPoint.x);
								double trackTravelScreenX = max(1.0,
									static_cast<double>(trackEndPoint.x)
										- trackStartScreenX);
								double rangeSpan =
									static_cast<double>(
										range.max - range.min);
								double unitTravelScreen = max(0.000001,
									ResolveThicknessFineDialUnitTravel(
										trackTravelScreenX, barStyle.dpiZoom));
								double initialNormalized =
									range.max > range.min
										? clamp(
											(static_cast<double>(initialWidth)
												- range.min) / rangeSpan,
											0.0, 1.0)
										: 0.0;
								double thumbCenterScreenX =
									trackStartScreenX
									+ initialNormalized * trackTravelScreenX;
								// 以渲染后的圆点透明度为准：未完全出现时忽略一切改值手势。
								bool thumbFullyVisible = sliderThumb
									&& sliderThumb->w.val > 0.0
									&& sliderThumb->h.val > 0.0
									&& static_cast<double>(sliderThumb->pct.val)
										>= 0.999999;
								bool valueAdjustAllowed =
									fineDialGesture
									|| sliderPopupGesture
									|| (sliderAlreadyShown && thumbFullyVisible);
								// 点在圆点内：保持相对偏移；点在外侧：中心对齐到触点 X。
								bool pressOnThumb = !fineDialGesture
									&& valueAdjustAllowed
									&& sliderThumb->IsClick(
										msg.x, msg.y, barStyle.zoom);
								bool pressTracksSliderThumb = pressOnThumb
									|| sliderPopupGesture;
								double grabOffsetScreenX = pressTracksSliderThumb
									? (pressScreenX - thumbCenterScreenX)
									: 0.0;
								auto ProjectWidthFromScreenX =
									[&](double screenX) -> float
									{
										// screenX 表示目标圆点中心的屏幕横坐标。
										double rawWidth = range.min
											+ (screenX - trackStartScreenX)
												/ trackTravelScreenX * rangeSpan;
										double clampedWidth = clamp(
											rawWidth,
											static_cast<double>(range.min),
											static_cast<double>(range.max));
										return static_cast<float>(clamp(
											static_cast<int>(lround(
												clampedWidth)),
											range.min, range.max));
									};
								auto ProjectRelativePreviewWidth =
									[&](double screenX) -> float
									{
										double rawWidth = static_cast<double>(
											initialWidth)
											+ (screenX - pressScreenX)
												/ (trackTravelScreenX
													* BarThicknessPreviewTouchDragTravelScale)
												* rangeSpan;
										double clampedWidth = clamp(rawWidth,
											static_cast<double>(range.min),
											static_cast<double>(range.max));
										return static_cast<float>(clamp(
											static_cast<int>(lround(clampedWidth)),
											range.min, range.max));
									};
								auto ApplyCandidateWidth =
									[&](float targetWidth,
										bool candidateAllowed) -> bool
									{
										if (!candidateAllowed) return false;
										if (barState.drawAttributeBar
											.thicknessSliderHoldLocked)
											return false;

										int roundedWidth = static_cast<int>(
											lround(targetWidth));
										if (roundedWidth == lastCandidateWidth
											&& candidateWidthIsInteger)
											return false;

										lastCandidateWidth = roundedWidth;
										candidateWidthIsInteger = true;
										finalWidth = static_cast<float>(
											roundedWidth);
										candidateChanged = abs(
											static_cast<double>(
												finalWidth - initialWidth))
											> 0.000001;
										barState.drawAttributeBar
											.thicknessSliderCandidateWidth =
											finalWidth;
										return true;
									};
							double fineDialAnchorScreenX = pressScreenX;
							double fineDialAnchorValue =
								static_cast<double>(initialWidth);
							double fineDialPressStartValue =
								static_cast<double>(initialWidth);
							if (viewModeAtPress == ThicknessViewMode::FineDial)
							{
								double visualSnapshot = static_cast<double>(
									barState.drawAttributeBar
										.thicknessFineDialVisualWidth);
								if (isfinite(visualSnapshot) && visualSnapshot > 0.0)
									fineDialPressStartValue = visualSnapshot;
							}
							auto ApplyFineDialScreenX = [&](double screenX)
									{
										if (!fineDialGesture
											|| barState.drawAttributeBar
												.thicknessSliderHoldLocked)
											return false;
										double nextRawValue = fineDialAnchorValue
											- (screenX - fineDialAnchorScreenX)
												/ max(0.000001, unitTravelScreen);
										bool visualChanged = abs(nextRawValue
											- thicknessFineDialRawValue) > 0.000001;
										thicknessFineDialRawValue = nextRawValue;
										int candidate =
											PublishThicknessFineDialCandidate();
										lastCandidateWidth = candidate;
										candidateWidthIsInteger = true;
										finalWidth = static_cast<float>(candidate);
										candidateChanged = abs(
											static_cast<double>(finalWidth - initialWidth))
											> 0.000001;
										AddThicknessFineDialSample(
											screenX, GetTickCount64());
										return visualChanged;
									};

									// 拖动改值后静止保持：0.5s 出提示，再 1.5s（合计 2.0s）锁定。
									double stillThresholdPx =
										BarThicknessHoldStillnessPx
										* max(1.0, static_cast<double>(
											barStyle.dpiZoom));
									double lastMoveScreenX = pressScreenX;
									double lastMoveScreenY =
										static_cast<double>(startScreenPoint.y);
									// 触摸静止帧没有新消息，保留最后触点推进 Hold，不能读取鼠标光标。
									double lastTouchScreenX = pressScreenX;
									double lastTouchScreenY = pressScreenY;
									bool holdStillTracking = false;
									ULONGLONG holdStillStartTick = 0;
									// 仅在“尚未锁定”时允许取消提示；锁定后必须等松手。
									auto ResetHoldLockState = [&]()
										{
											if (barState.drawAttributeBar
												.thicknessSliderHoldLocked)
												return;

											holdStillTracking = false;
											holdStillStartTick = 0;
											if (barState.drawAttributeBar
												.thicknessSliderHoldHintActive
												|| static_cast<float>(
													barState.drawAttributeBar
														.thicknessSliderHoldProgress)
													> 0.0f)
											{
												barState.drawAttributeBar
													.thicknessSliderHoldHintActive =
													false;
												barState.drawAttributeBar
													.thicknessSliderHoldProgress =
													0.0f;
												UpdateRendering(false);
											}
										};
									auto UpdateHoldLockState = [&](double screenX,
										double screenY)
										{
											// 已锁定：忽略一切位移，直到抬起才解锁。
											if (barState.drawAttributeBar
												.thicknessSliderHoldLocked)
												return;

											bool holdLockEligible = valueAdjustAllowed
												|| (directTouchPreviewGesture
													&& gestureDragged);
											if (!candidateChanged
												|| !holdLockEligible
												|| !msg.lbutton)
											{
												ResetHoldLockState();
												return;
											}

											double moveDx = screenX - lastMoveScreenX;
											double moveDy = screenY - lastMoveScreenY;
											double moveDist = sqrt(
												moveDx * moveDx + moveDy * moveDy);
											if (moveDist > stillThresholdPx)
											{
												lastMoveScreenX = screenX;
												lastMoveScreenY = screenY;
												ResetHoldLockState();
												return;
											}

											ULONGLONG nowTick = GetTickCount64();
											if (!holdStillTracking)
											{
												holdStillTracking = true;
												holdStillStartTick = nowTick;
											}
											ULONGLONG stillMs =
												nowTick - holdStillStartTick;
											bool needRender = false;
											if (stillMs >= BarThicknessHoldHintDelayMs)
											{
												if (!barState.drawAttributeBar
													.thicknessSliderHoldHintActive)
												{
													barState.drawAttributeBar
														.thicknessSliderHoldHintActive =
														true;
													needRender = true;
												}
												double lockProgress = clamp(
													static_cast<double>(
														stillMs
															- BarThicknessHoldHintDelayMs)
													/ static_cast<double>(
														BarThicknessHoldLockDelayMs),
													0.0, 1.0);
												float progressValue =
													static_cast<float>(lockProgress);
												if (abs(static_cast<double>(
													barState.drawAttributeBar
														.thicknessSliderHoldProgress)
													- lockProgress) > 0.001)
												{
													barState.drawAttributeBar
														.thicknessSliderHoldProgress =
														progressValue;
													needRender = true;
												}
												if (lockProgress >= 1.0
													&& !barState.drawAttributeBar
														.thicknessSliderHoldLocked)
												{
												// 锁定：冻结候选；Slider 视觉或直接触摸 Preview 保持到真实抬手。
													barState.drawAttributeBar
														.thicknessSliderHoldLocked =
														true;
													barState.drawAttributeBar
														.thicknessSliderHoldProgress =
														1.0f;
													// 锁定后不再跟踪静止位移，避免误清锁。
													holdStillTracking = false;
													needRender = true;
												}
											}
											else if (barState.drawAttributeBar
												.thicknessSliderHoldHintActive
												|| static_cast<float>(
													barState.drawAttributeBar
														.thicknessSliderHoldProgress)
													> 0.0f)
											{
												barState.drawAttributeBar
													.thicknessSliderHoldHintActive =
													false;
												barState.drawAttributeBar
													.thicknessSliderHoldProgress =
													0.0f;
												needRender = true;
											}
											if (needRender) UpdateRendering(false);
										};
									bool fineActivationDwellTracking = false;
									bool fineActivationDwellAnchorValid = false;
									int fineActivationDwellAnchorClientX = 0;
									int fineActivationDwellAnchorClientY = 0;
									bool fineActivationRecognitionActive = false;
									ULONGLONG fineActivationDwellStartTick = 0;
									auto PublishFineActivationPreview = [&](bool recognitionActive,
										bool dwellActive, float progress)
										{
											progress = clamp(progress, 0.0f, 1.0f);
											bool changed = static_cast<bool>(barState.drawAttributeBar
												.thicknessFineDialActivationPreviewActive) != recognitionActive
												|| static_cast<bool>(barState.drawAttributeBar
													.thicknessFineDialActivationDwellActive) != dwellActive
												|| abs(static_cast<double>(barState.drawAttributeBar
													.thicknessFineDialActivationPreviewProgress)
													- progress) > 0.001;
											barState.drawAttributeBar
												.thicknessFineDialActivationPreviewActive = recognitionActive;
											barState.drawAttributeBar
												.thicknessFineDialActivationDwellActive = dwellActive;
											barState.drawAttributeBar
												.thicknessFineDialActivationPreviewProgress = progress;
											if (changed) UpdateRendering(false);
										};
									auto ResetFineActivationDwell = [&]()
										{
											fineActivationDwellTracking = false;
											fineActivationDwellAnchorValid = false;
											fineActivationDwellStartTick = 0;
											PublishFineActivationPreview(
												fineActivationRecognitionActive, false, 0.0f);
										};
									auto EndFineActivationRecognition = [&]()
										{
											fineActivationRecognitionActive = false;
											fineActivationDwellTracking = false;
											fineActivationDwellAnchorValid = false;
											fineActivationDwellStartTick = 0;
											PublishFineActivationPreview(false, false, 0.0f);
										};
									auto BeginFineActivationRecognition = [&]()
										{
											if (fineActivationRecognitionActive) return;
											fineActivationRecognitionActive = true;
											// Slider 捕获建立即锁存刻度锚点，按住期间基础预览保持可见。
											barState.drawAttributeBar
												.thicknessFineDialActivationPreviewVisualWidth =
												static_cast<float>(finalWidth);
											PublishFineActivationPreview(true, false, 0.0f);
										};
									auto ActivateFineDialDrag = [&](double screenX,
										double startValue,
										bool preserveActivationPreview)
										{
											ResetHoldLockState();
											if (!fineDialPopupReturnGesture
												&& barState.drawAttributeBar.thicknessViewMode
													!= ThicknessViewMode::FineDial)
											{
												// FineDial 暂时隐藏 Slider 固定态，返回时恢复进入前的生命周期。
												thicknessFineDialReturnPinned = pinnedAtPress;
											}
										if (preserveActivationPreview)
										{
											// 完整 dwell 作为一次性交接值，直到 renderer 的 FineDial 帧消费。
											fineActivationRecognitionActive = false;
											fineActivationDwellTracking = false;
											fineActivationDwellAnchorValid = false;
											fineActivationDwellStartTick = 0;
											barState.drawAttributeBar
												.thicknessFineDialActivationPreviewActive = false;
											barState.drawAttributeBar
												.thicknessFineDialActivationDwellActive = false;
											barState.drawAttributeBar
												.thicknessFineDialActivationPreviewProgress = 1.0f;
										}
										else EndFineActivationRecognition();
											fineDragActivationArmed = false;
											fineDialGesture = true;
											precisionRelativeGesture = false;
											gestureDragged = true;
											barState.drawAttributeBar.thicknessViewMode =
												ThicknessViewMode::FineDial;
											barState.drawAttributeBar.thicknessSliderHover = false;
											barState.drawAttributeBar.thicknessSliderPinned = false;
											barState.drawAttributeBar.thicknessSliderDragging = false;
											barState.drawAttributeBar.thicknessSliderPressed = false;
											CloseThicknessOverflowTooltip();
											BeginThicknessFineDialDrag(startValue,
												screenX, unitTravelScreen, range);
											fineDialAnchorScreenX = screenX;
											fineDialAnchorValue = static_cast<double>(
												barState.drawAttributeBar
													.thicknessFineDialVisualWidth);
											lastCandidateWidth = static_cast<int>(lround(
												static_cast<double>(barState.drawAttributeBar
													.thicknessSliderCandidateWidth)));
											finalWidth = static_cast<float>(lastCandidateWidth);
											candidateChanged = abs(static_cast<double>(
												finalWidth - initialWidth)) > 0.000001;
											UpdateRendering(false);
										};
									auto PopupDragThresholdExceeded = [&](double screenX,
										double screenY)
										{
											double moveDx = screenX - pressScreenX;
											double moveDy = screenY - pressScreenY;
											return moveDx * moveDx + moveDy * moveDy
												> touchDragThresholdSquared;
										};
									auto BeginSliderFromFineDialPopup = [&](double screenX)
										{
											// 先提交 FineDial 当前候选，再以切换当帧重新锚定，避免粗细跳变。
											CommitThicknessFineDialSelection();
											initialWidth = GetPenWidth();
											finalWidth = initialWidth;
											lastCandidateWidth = static_cast<int>(
												lround(initialWidth));
											candidateWidthIsInteger = abs(static_cast<double>(
												initialWidth) - lastCandidateWidth) <= 0.000001;
											candidateChanged = false;
											double committedNormalized = range.max > range.min
												? clamp((static_cast<double>(initialWidth) - range.min)
													/ rangeSpan, 0.0, 1.0)
												: 0.0;
											double committedThumbCenter = trackStartScreenX
												+ committedNormalized * trackTravelScreenX;
											grabOffsetScreenX = screenX - committedThumbCenter;
											valueAdjustAllowed = true;
											gestureDragged = true;
											barState.drawAttributeBar.thicknessViewMode =
												ThicknessViewMode::Slider;
											barState.drawAttributeBar.thicknessSliderHover = false;
											barState.drawAttributeBar.thicknessSliderPinned = false;
											barState.drawAttributeBar.thicknessSliderPressed = true;
											barState.drawAttributeBar.thicknessSliderDragging = true;
											barState.drawAttributeBar.thicknessPreviewDragging = false;
											BeginFineActivationRecognition();
											UpdateRendering(false);
										};
									auto UpdateFineActivationDwell = [&](double screenX,
										int clientX, int clientY)
										{
										if (fineDialGesture || fineDragActivationArmed
											|| directTouchPreviewGesture
											|| !fineActivationRecognitionActive)
										{
											ResetFineActivationDwell();
											return false;
										}
										// 仅 Popup 起手时由 Surface 保留 Hold；普通 Slider 拖动仍让识别区优先。
										bool popupOwnsPoint = previewPopupGesture
											&& IsBarThicknessPreviewPopupHit(
												barUISet, clientX, clientY);
										bool inActivationZone = !popupOwnsPoint
											&& IsBarThicknessFineDialDwellZone(
												barUISet, clientX, clientY);
										if (!inActivationZone)
										{
											ResetFineActivationDwell();
											return false;
										}
										// 激活等待优先于 Hold，进入外区即隐藏并重置锁定提示。
										ResetHoldLockState();
										ULONGLONG nowTick = GetTickCount64();
										if (!fineActivationDwellTracking)
										{
											fineActivationDwellTracking = true;
											fineActivationDwellAnchorValid = true;
											fineActivationDwellAnchorClientX = clientX;
											fineActivationDwellAnchorClientY = clientY;
											fineActivationDwellStartTick = nowTick;
											PublishFineActivationPreview(true, true, 0.0f);
											return false;
										}
										// 任一轴累计超过 5 DIP 都从当前位置重新开始静止计时。
										if (!fineActivationDwellAnchorValid
											|| abs(static_cast<double>(clientX
												- fineActivationDwellAnchorClientX))
												> touchDragThresholdPx
											|| abs(static_cast<double>(clientY
												- fineActivationDwellAnchorClientY))
												> touchDragThresholdPx)
										{
											fineActivationDwellAnchorValid = true;
											fineActivationDwellAnchorClientX = clientX;
											fineActivationDwellAnchorClientY = clientY;
											fineActivationDwellStartTick = nowTick;
											PublishFineActivationPreview(true, true, 0.0f);
											return false;
										}
										ULONGLONG dwellMs = nowTick
											- fineActivationDwellStartTick;
										PublishFineActivationPreview(true, true, static_cast<float>(
											clamp(static_cast<double>(dwellMs)
												/ static_cast<double>(
													BarThicknessFineDialActivationDwellMs),
												0.0, 1.0)));
										if (dwellMs < BarThicknessFineDialActivationDwellMs)
											return false;
										// 切换当帧先吃掉当前 X，避免 handoff 使用上一条 move 的候选。
										ApplyCandidateWidth(
											precisionRelativeGesture
												? ProjectRelativePreviewWidth(screenX)
												: ProjectWidthFromScreenX(
													screenX - grabOffsetScreenX),
											valueAdjustAllowed);
										ActivateFineDialDrag(screenX,
											static_cast<double>(finalWidth), true);
										return true;
										};

									if (!fineDialGesture)
										barState.drawAttributeBar
											.thicknessSliderCandidateWidth = initialWidth;
									barState.drawAttributeBar
										.thicknessPreviewDragging = false;
									if (!directTouchPreviewGesture
										&& !fineDialGesture
										&& !fineDialPopupReturnGesture)
										barState.drawAttributeBar.thicknessViewMode =
											ThicknessViewMode::Slider;
									barState.drawAttributeBar.thicknessSliderPressed =
										!directTouchPreviewGesture
										&& !fineDragActivationArmed
										&& !fineDialPopupReturnGesture;
									if (!directTouchPreviewGesture && pressTracksSliderThumb)
									{
										// Thumb 按下即结束固定态；Pressed 继续维持本次 Slider 交互。
										barState.drawAttributeBar.thicknessSliderPinned = false;
									}
									barState.drawAttributeBar
										.thicknessSliderHoldHintActive = false;
									barState.drawAttributeBar
										.thicknessSliderHoldLocked = false;
									barState.drawAttributeBar
										.thicknessSliderHoldProgress = 0.0f;
									if (fineDialPopupReturnGesture)
									{
										// 抓住 FineDial 浮窗时暂停候选/惯性，保持画面等待点击或拖动分类。
										BeginThicknessFineDialDrag(initialWidth,
											pressScreenX, unitTravelScreen, range);
										fineDialAnchorScreenX = pressScreenX;
										fineDialAnchorValue = static_cast<double>(
											barState.drawAttributeBar
												.thicknessFineDialVisualWidth);
										lastCandidateWidth = static_cast<int>(lround(
											static_cast<double>(barState.drawAttributeBar
												.thicknessSliderCandidateWidth)));
										finalWidth = static_cast<float>(lastCandidateWidth);
										candidateChanged = abs(static_cast<double>(
											finalWidth - initialWidth)) > 0.000001;
									}
									else if (fineDialGesture)
										ActivateFineDialDrag(
											pressScreenX, fineDialPressStartValue, false);
									// 圆点完全出现且点在外侧时，按下即跳到点击位置；点在圆点内只开始抓取。
									if (!directTouchPreviewGesture
										&& !fineDialGesture
										&& !fineDragActivationArmed
										&& !precisionRelativeGesture
										&& valueAdjustAllowed && !pressTracksSliderThumb)
									{
										ApplyCandidateWidth(
											ProjectWidthFromScreenX(pressScreenX),
											true);
										barState.drawAttributeBar
											.thicknessSliderDragging = true;
									}
									if (!directTouchPreviewGesture
										&& !fineDialGesture
										&& !fineDragActivationArmed
										&& !previewPopupGesture)
										BeginFineActivationRecognition();
									StopIndependentHover(
										hoveredIndependentButton, true, true);
									hoveredIndependentButton =
										IndependentHoverTargetEnum::None;
									SendMessage(floating_window,
										BarThicknessSliderCaptureMessage,
										BarThicknessSliderCaptureStart, 0);
									UpdateRendering(false);

									while (!offSignal)
									{
										// 用 peek 轮询，便于静止计时在无新消息时也能推进。
										if (!TryGetBarInteractionMessage(
											&msg, EM_MOUSE, true, floating_window))
										{
											bool samePenMode =
												stateMode.StateModeSelect
													== StateModeSelectEnum::IdtPen
												&& stateMode.Pen.ModeSelect
													== gesturePenMode;
											if (!samePenMode
												|| !barState.drawAttribute
												|| barState.fold)
											{
												penModeChanged = !samePenMode;
												break;
											}
											if (!Inkeys::Inputs::IsKeyBoardDown(
												VK_LBUTTON)
												&& !msg.lbutton)
												break;

											if (pointerUsesTouchCoordinates)
											{
												// 合成触摸不保证系统光标同步，Hold 只使用最后一条触点坐标。
												if (!fineDialPopupReturnGesture
													|| gestureDragged)
												{
													POINT clientPoint{
														static_cast<LONG>(lastTouchScreenX),
														static_cast<LONG>(lastTouchScreenY) };
													bool fineActivated = BarScreenToLayout(
														clientPoint) && UpdateFineActivationDwell(
															lastTouchScreenX,
															clientPoint.x, clientPoint.y);
													if (!fineActivated
														&& !fineActivationDwellTracking
														&& !fineDragActivationArmed)
													{
														UpdateHoldLockState(
															lastTouchScreenX,
															lastTouchScreenY);
													}
												}
											}
											else if (!fineDialPopupReturnGesture
												|| gestureDragged)
											{
											POINT cursorPoint{};
											if (GetCursorPos(&cursorPoint))
											{
												POINT clientPoint = cursorPoint;
														BarScreenToLayout(clientPoint);
												bool fineActivated = UpdateFineActivationDwell(
													static_cast<double>(cursorPoint.x),
													clientPoint.x, clientPoint.y);
												if (!fineActivated
													&& !fineActivationDwellTracking
													&& !fineDragActivationArmed)
													UpdateHoldLockState(
														static_cast<double>(cursorPoint.x),
														static_cast<double>(cursorPoint.y));
												}
											}
											std::this_thread::sleep_for(
												std::chrono::milliseconds(8));
											continue;
										}

										if (IsBarTouchCancelMessage(msg))
										{
											touchGestureCancelled = true;
											break;
										}

										bool samePenMode =
											stateMode.StateModeSelect
												== StateModeSelectEnum::IdtPen
											&& stateMode.Pen.ModeSelect
												== gesturePenMode;
										if (!samePenMode
											|| !barState.drawAttribute
											|| barState.fold)
										{
											penModeChanged = !samePenMode;
											break;
										}

										if (msg.message == WM_MOUSEMOVE
											&& msg.lbutton)
										{
											POINT screenPoint{
												static_cast<LONG>(msg.x),
												static_cast<LONG>(msg.y) };
											BarLayoutToScreen(screenPoint);
											double screenX =
												static_cast<double>(
													screenPoint.x);
											double screenY =
												static_cast<double>(
													screenPoint.y);
											if (pointerUsesTouchCoordinates)
											{
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
											}
											if (fineDragActivationArmed)
											{
												if (abs(screenX - pressScreenX)
													> touchDragThresholdPx)
												{
													// 过水平 slop 的当帧重新锚定，累计位移不参与改值。
													ActivateFineDialDrag(screenX,
														static_cast<double>(finalWidth), false);
												}
												ResetHoldLockState();
												continue;
											}
											if (fineDialGesture)
											{
												if (screenX != fineDialAnchorScreenX)
													gestureDragged = true;
												if (ApplyFineDialScreenX(screenX))
												{
													lastMoveScreenX = screenX;
													lastMoveScreenY = screenY;
													ResetHoldLockState();
													UpdateRendering(false);
												}
												else UpdateHoldLockState(screenX, screenY);
												continue;
											}
											if (fineDialPopupReturnGesture)
											{
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
												if (!gestureDragged
													&& PopupDragThresholdExceeded(
														screenX, screenY))
												{
													// 越过二维 slop 后永久切为 Slider；本帧只锚定，不改值。
													BeginSliderFromFineDialPopup(screenX);
													ResetHoldLockState();
													continue;
												}
												if (gestureDragged
													&& ApplyCandidateWidth(
														ProjectWidthFromScreenX(
															screenX - grabOffsetScreenX),
														true))
												{
													lastMoveScreenX = screenX;
													lastMoveScreenY = screenY;
													ResetHoldLockState();
													UpdateRendering(false);
												}
												else if (gestureDragged)
													UpdateHoldLockState(screenX, screenY);
												continue;
											}
											if (sliderPopupGesture)
											{
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
												if (!gestureDragged
													&& PopupDragThresholdExceeded(
														screenX, screenY))
												{
													// 浮窗与 Thumb 共用绝对投影，并保留按下时的 X 抓取偏移。
													gestureDragged = true;
													barState.drawAttributeBar
														.thicknessSliderDragging = true;
													BeginFineActivationRecognition();
													UpdateRendering(false);
												}
												bool fineActivated = UpdateFineActivationDwell(
													screenX, msg.x, msg.y);
												if (fineActivated)
													continue;
												if (gestureDragged
													&& ApplyCandidateWidth(
														ProjectWidthFromScreenX(
															screenX - grabOffsetScreenX),
														true))
												{
													lastMoveScreenX = screenX;
													lastMoveScreenY = screenY;
													ResetHoldLockState();
													UpdateRendering(false);
												}
												else if (gestureDragged)
													UpdateHoldLockState(screenX, screenY);
												continue;
											}
											if (directTouchPreviewGesture)
											{
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
												double moveDx =
													screenX - pressScreenX;
												double moveDy =
													screenY - pressScreenY;
												if (!gestureDragged
													&& moveDx * moveDx + moveDy * moveDy
														> touchDragThresholdSquared)
												{
													// 超过 5 DIP 后永久归类为 Preview 拖动，回到原点也不再触发点击。
													gestureDragged = true;
													barState.drawAttributeBar
														.thicknessPreviewDragging = true;
													UpdateRendering(false);
												}
												if (gestureDragged)
												{
													if (ApplyCandidateWidth(
														ProjectRelativePreviewWidth(screenX),
														true))
													{
														// 有效改值后重新起算静止会话；未改值则继续累计。
														lastMoveScreenX = screenX;
														lastMoveScreenY = screenY;
														ResetHoldLockState();
														UpdateRendering(false);
													}
													else UpdateHoldLockState(screenX, screenY);
												}
												continue;
											}
										if (precisionRelativeGesture)
										{
											if (!gestureDragged
												&& screenX != pressScreenX)
											{
												gestureDragged = true;
												barState.drawAttributeBar
													.thicknessSliderDragging = true;
												UpdateRendering(false);
											}
											bool fineActivated = UpdateFineActivationDwell(
												screenX, msg.x, msg.y);
											if (fineActivated)
												continue;
											if (gestureDragged
												&& !barState.drawAttributeBar
													.thicknessSliderHoldLocked
												&& ApplyCandidateWidth(
													ProjectRelativePreviewWidth(screenX),
													valueAdjustAllowed))
											{
												lastMoveScreenX = screenX;
												lastMoveScreenY = screenY;
												ResetHoldLockState();
												UpdateRendering(false);
											}
											else if (!fineActivationDwellTracking)
												UpdateHoldLockState(screenX, screenY);
											continue;
										}
										if (!gestureDragged
											&& screenX != pressScreenX)
										{
											// 仅水平位移会把按下手势切换为真实的滑轨拖动。
											gestureDragged = true;
											// 圆点未完全出现时，滑动只用于区分点击/拖动，不进入候选拖动态。
											if (valueAdjustAllowed
												&& !barState.drawAttributeBar
													.thicknessSliderDragging)
											{
												barState.drawAttributeBar
													.thicknessSliderDragging = true;
												UpdateRendering(false);
											}
										}
										bool fineActivated = UpdateFineActivationDwell(
											screenX, msg.x, msg.y);
										if (fineActivated)
											continue;

											if (gestureDragged
												&& !barState.drawAttributeBar
													.thicknessSliderHoldLocked
												&& ApplyCandidateWidth(
													ProjectWidthFromScreenX(
														screenX - grabOffsetScreenX),
													valueAdjustAllowed))
											{
												// 改值后重置静止保持计时。
												lastMoveScreenX = screenX;
												lastMoveScreenY = screenY;
												holdStillTracking = false;
												holdStillStartTick = 0;
												if (barState.drawAttributeBar
													.thicknessSliderHoldHintActive
													|| barState.drawAttributeBar
														.thicknessSliderHoldLocked
													|| static_cast<float>(
														barState.drawAttributeBar
															.thicknessSliderHoldProgress)
														> 0.0f)
												{
													barState.drawAttributeBar
														.thicknessSliderHoldHintActive =
														false;
													barState.drawAttributeBar
														.thicknessSliderHoldLocked =
														false;
													barState.drawAttributeBar
														.thicknessSliderHoldProgress =
														0.0f;
												}
												UpdateRendering(false);
											}
											else if (!fineActivationDwellTracking)
											{
												UpdateHoldLockState(
													screenX, screenY);
											}
											continue;
										}
										if (msg.message == WM_LBUTTONUP
											|| !msg.lbutton)
										{
											if (fineDialGesture)
											{
												POINT screenPoint{
													static_cast<LONG>(msg.x),
													static_cast<LONG>(msg.y) };
														BarLayoutToScreen(screenPoint);
												ApplyFineDialScreenX(
													static_cast<double>(screenPoint.x));
											}
											else if (fineDialPopupReturnGesture)
											{
												POINT screenPoint{
													static_cast<LONG>(msg.x),
													static_cast<LONG>(msg.y) };
														BarLayoutToScreen(screenPoint);
												double screenX = static_cast<double>(screenPoint.x);
												double screenY = static_cast<double>(screenPoint.y);
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
												bool sliderDragAlreadyStarted = gestureDragged;
												if (!gestureDragged
													&& PopupDragThresholdExceeded(
														screenX, screenY))
												{
													BeginSliderFromFineDialPopup(screenX);
												}
												else if (sliderDragAlreadyStarted)
												{
													ApplyCandidateWidth(
														ProjectWidthFromScreenX(
															screenX - grabOffsetScreenX),
														true);
												}
											}
											else if (sliderPopupGesture)
											{
												POINT screenPoint{
													static_cast<LONG>(msg.x),
													static_cast<LONG>(msg.y) };
														BarLayoutToScreen(screenPoint);
												double screenX = static_cast<double>(screenPoint.x);
												double screenY = static_cast<double>(screenPoint.y);
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
												if (!gestureDragged
													&& PopupDragThresholdExceeded(
														screenX, screenY))
												{
													gestureDragged = true;
													barState.drawAttributeBar
														.thicknessSliderDragging = true;
												}
												if (gestureDragged)
												{
													ApplyCandidateWidth(
														ProjectWidthFromScreenX(
															screenX - grabOffsetScreenX),
														true);
												}
											}
											else if (directTouchPreviewGesture)
											{
												POINT screenPoint{
													static_cast<LONG>(msg.x),
													static_cast<LONG>(msg.y) };
														BarLayoutToScreen(screenPoint);
												double moveDx = static_cast<double>(
													screenPoint.x) - pressScreenX;
												double moveDy = static_cast<double>(
													screenPoint.y) - pressScreenY;
												if (!gestureDragged
													&& moveDx * moveDx + moveDy * moveDy
														> touchDragThresholdSquared)
												{
													gestureDragged = true;
													barState.drawAttributeBar
														.thicknessPreviewDragging = true;
												}
												if (gestureDragged)
													ApplyCandidateWidth(
														ProjectRelativePreviewWidth(
															static_cast<double>(screenPoint.x)),
														true);
											}
											else if (precisionRelativeGesture)
											{
												POINT screenPoint{
													static_cast<LONG>(msg.x),
													static_cast<LONG>(msg.y) };
														BarLayoutToScreen(screenPoint);
												if (!gestureDragged
													&& static_cast<double>(screenPoint.x)
														!= pressScreenX)
													gestureDragged = true;
												if (gestureDragged)
													ApplyCandidateWidth(
														ProjectRelativePreviewWidth(
															static_cast<double>(screenPoint.x)),
														valueAdjustAllowed);
											}
											break;
										}

										// 其他鼠标消息仍更新静止计时。
										if (pointerUsesTouchCoordinates)
										{
											if (!fineDialPopupReturnGesture
												|| gestureDragged)
											{
												POINT clientPoint{
													static_cast<LONG>(lastTouchScreenX),
													static_cast<LONG>(lastTouchScreenY) };
														bool fineActivated = BarScreenToLayout(
															clientPoint) && UpdateFineActivationDwell(
														lastTouchScreenX,
														clientPoint.x, clientPoint.y);
												if (!fineActivated
													&& !fineActivationDwellTracking
													&& !fineDragActivationArmed)
												{
													UpdateHoldLockState(
														lastTouchScreenX,
														lastTouchScreenY);
												}
											}
										}
										else if ((!fineDialPopupReturnGesture
												|| gestureDragged)
											&& !fineDragActivationArmed
											&& !fineActivationDwellTracking)
										{
											POINT cursorPoint{};
											if (GetCursorPos(&cursorPoint))
											{
												UpdateHoldLockState(
													static_cast<double>(cursorPoint.x),
													static_cast<double>(cursorPoint.y));
											}
										}
									}

									bool gestureCaptured = barState.drawAttributeBar
										.thicknessSliderCapture;
									if (gestureCaptured)
										SendMessage(floating_window,
											BarThicknessSliderCaptureMessage,
											BarThicknessSliderCaptureStop, 0);
									bool holdLockedAtRelease =
										barState.drawAttributeBar
											.thicknessSliderHoldLocked;
									barState.drawAttributeBar
										.thicknessSliderPressed = false;
									// 真实松手后统一结束提示与锁定会话。
									barState.drawAttributeBar
										.thicknessSliderHoldHintActive = false;
									barState.drawAttributeBar
										.thicknessSliderHoldLocked = false;
									barState.drawAttributeBar
										.thicknessSliderHoldProgress = 0.0f;
									bool gestureCompleted = gestureCaptured
										&& !touchGestureCancelled
										&& !offSignal && !msg.lbutton
										&& barState.drawAttribute && !barState.fold
										&& stateMode.StateModeSelect
											== StateModeSelectEnum::IdtPen
										&& stateMode.Pen.ModeSelect
											== gesturePenMode;
									bool activationPreviewHandoffPending = fineDialGesture
										&& barState.drawAttributeBar.thicknessViewMode
											== ThicknessViewMode::FineDial
										&& !barState.drawAttributeBar
											.thicknessFineDialActivationPreviewActive
										&& static_cast<float>(barState.drawAttributeBar
											.thicknessFineDialActivationPreviewProgress) > 0.0f;
									// 正常完成保留 renderer 尚未消费的 handoff；取消路径必须清零。
									if (!gestureCompleted || !activationPreviewHandoffPending)
										EndFineActivationRecognition();
									bool keepFineDialAfterModeChange = fineDialGesture
										&& penModeChanged && !offSignal
										&& ThicknessSliderAvailable();
									if (fineDragActivationArmed)
									{
										barState.drawAttributeBar.thicknessSliderDragging = false;
										barState.drawAttributeBar.thicknessPreviewDragging = false;
										barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
										if (gestureCompleted)
										{
											// 未过 slop 的 Drag Zone 手势完整恢复按下前状态。
											barState.drawAttributeBar.thicknessViewMode =
												viewModeAtPress;
											barState.drawAttributeBar.thicknessSliderHover =
												hoverAtPress;
											barState.drawAttributeBar.thicknessSliderPinned =
												pinnedAtPress;
										}
										else
											CloseThicknessSlider(false);
									}
									else if (fineDialGesture)
									{
										barState.drawAttributeBar.thicknessSliderDragging = false;
										barState.drawAttributeBar.thicknessPreviewDragging = false;
										barState.drawAttributeBar.thicknessSliderHover = false;
										barState.drawAttributeBar.thicknessSliderPinned = false;
										if (gestureCompleted)
										{
											barState.drawAttributeBar.thicknessViewMode =
												ThicknessViewMode::FineDial;
											EndThicknessFineDialDrag(holdLockedAtRelease);
										}
										else
										{
											CancelThicknessFineDialSelection();
											if (keepFineDialAfterModeChange)
												barState.drawAttributeBar.thicknessViewMode =
													ThicknessViewMode::FineDial;
											else CloseThicknessSlider(false);
										}
									}
									else if (fineDialPopupReturnGesture)
									{
										barState.drawAttributeBar.thicknessSliderDragging = false;
										barState.drawAttributeBar.thicknessPreviewDragging = false;
										barState.drawAttributeBar.thicknessSliderHover = false;
										if (gestureCompleted)
										{
											if (!gestureDragged)
											{
												// FineDial 浮窗轻点在真实抬起时提交，并回到 Slider。
												CommitThicknessFineDialSelection();
											}
											else if (candidateChanged)
												SetPenWidth(finalWidth, true);
											barState.drawAttributeBar
												.thicknessSliderCandidateWidth = 0.0f;
											barState.drawAttributeBar.thicknessViewMode =
												ThicknessViewMode::Slider;
											barState.drawAttributeBar.thicknessSliderPinned =
												thicknessFineDialReturnPinned;
										}
										else
										{
											CancelThicknessFineDialSelection();
											CloseThicknessSlider(false);
										}
									}
									else if (sliderPopupGesture
										&& gestureCompleted && !gestureDragged)
									{
										barState.drawAttributeBar.thicknessSliderDragging = false;
										barState.drawAttributeBar.thicknessPreviewDragging = false;
										barState.drawAttributeBar
											.thicknessSliderCandidateWidth = 0.0f;
										// Slider 浮窗轻点只在抬起后进入 FineDial，避免按下即切换。
										ActivateFineDialDrag(
											pressScreenX, static_cast<double>(initialWidth), false);
										EndThicknessFineDialDrag(false);
									}
									else
									{
										bool canCommit = gestureCompleted
											&& candidateChanged;
										if (canCommit) SetPenWidth(finalWidth, true);
										barState.drawAttributeBar.thicknessSliderDragging = false;
										barState.drawAttributeBar.thicknessPreviewDragging = false;
										barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
										if (directTouchPreviewGesture)
										{
											// 点击在抬手时才展开；拖动结束后继续保留 Preview。
											barState.drawAttributeBar.thicknessSliderHover = false;
											barState.drawAttributeBar.thicknessSliderPinned =
												gestureCompleted && !gestureDragged;
										}
										else if (gestureCompleted && gestureDragged)
											barState.drawAttributeBar.thicknessSliderPinned =
												pinnedAtPress || !hoverAtPress;
										else if (gestureCompleted && !pressTracksSliderThumb)
											barState.drawAttributeBar.thicknessSliderPinned = true;
										barState.drawAttributeBar.thicknessViewMode =
											gestureCompleted
												&& (barState.drawAttributeBar.thicknessSliderPinned
													|| barState.drawAttributeBar.thicknessSliderHover)
												? ThicknessViewMode::Slider
												: ThicknessViewMode::Preview;
									}
								if (gestureCompleted
									&& !fineDialGesture && !fineDragActivationArmed)
								{
									auto annotationInfoHit = shapeMap[
										BarUISetShapeEnum::
											DrawAttributeBar_ThicknessAnnotationInfoHit];
									auto overflowInfoHit = shapeMap[
										BarUISetShapeEnum::
											DrawAttributeBar_ThicknessOverflowInfoHit];
									auto annotationPopup = shapeMap[
										BarUISetShapeEnum::
											DrawAttributeBar_ThicknessAnnotationPopup];
									auto overflowPopup = shapeMap[
										BarUISetShapeEnum::
											DrawAttributeBar_ThicknessOverflowPopup];
									bool annotationPopupInteractive =
										barState.drawAttributeBar.thicknessAnnotationHover
										|| barState.drawAttributeBar.thicknessAnnotationHoverGrace
										|| barState.drawAttributeBar.thicknessAnnotationPinned;
									bool overflowPopupInteractive =
										barState.drawAttributeBar.thicknessOverflowHover
										|| barState.drawAttributeBar.thicknessOverflowHoverGrace
										|| barState.drawAttributeBar.thicknessOverflowPinned;
									bool annotationPointerInside =
										(annotationInfoHit && annotationInfoHit->IsClick(
											msg.x, msg.y, barStyle.zoom))
										|| (annotationPopupInteractive && annotationPopup
											&& annotationPopup->IsClick(
											msg.x, msg.y, barStyle.zoom));
					bool overflowPointerInside = !annotationPointerInside
						&& ((overflowInfoHit && overflowInfoHit->IsClick(
							msg.x, msg.y, barStyle.zoom))
							|| (overflowPopupInteractive && overflowPopup
								&& overflowPopup->IsClick(
								msg.x, msg.y, barStyle.zoom)));
									// 内层捕获循环不会处理普通 hover，抬起后统一重算提示和宽限期。
									UpdateTooltipHover(
										AnnotationTooltipAvailable(),
										annotationPointerInside,
										barState.drawAttributeBar.thicknessAnnotationHover,
										barState.drawAttributeBar.thicknessAnnotationPinned,
										barState.drawAttributeBar.thicknessAnnotationHoverGrace,
										BarThicknessAnnotationTooltipGraceTimerId);
									UpdateTooltipHover(
										OverflowTooltipAvailable(),
										overflowPointerInside,
										barState.drawAttributeBar.thicknessOverflowHover,
										barState.drawAttributeBar.thicknessOverflowPinned,
										barState.drawAttributeBar.thicknessOverflowHoverGrace,
										BarThicknessOverflowTooltipGraceTimerId);
					bool overflowUiActive =
						barState.drawAttributeBar.thicknessOverflowHover
						|| barState.drawAttributeBar.thicknessOverflowHoverGrace
						|| barState.drawAttributeBar.thicknessOverflowPinned;
									if (directTouchPreviewGesture)
										barState.drawAttributeBar.thicknessSliderHover = false;
									else
										barState.drawAttributeBar.thicknessSliderHover =
											!overflowUiActive
											&& ((sliderHit && sliderHit->IsClick(
												msg.x, msg.y, barStyle.zoom))
												|| IsBarThicknessPreviewPopupHit(
													barUISet, msg.x, msg.y)
												|| IsBarThicknessPrecisionDragHit(
													barUISet, msg.x, msg.y));
									barState.drawAttributeBar.thicknessViewMode =
										barState.drawAttributeBar.thicknessSliderPinned
											|| barState.drawAttributeBar.thicknessSliderHover
											? ThicknessViewMode::Slider
											: ThicknessViewMode::Preview;
								}
								else if (!gestureCompleted
									&& !fineDialGesture && !fineDragActivationArmed)
									CloseThicknessSlider(false);
								if (!ThicknessSliderAvailable()
									|| (penModeChanged && !fineDialGesture))
									CloseThicknessSlider(false);
								UpdateRendering();
								SuppressHoverUntilPointerMove();
									ClearBarInteractionMessages(
									EM_MOUSE, floating_window);
							}
						}
					}
				}
			return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult HandleDrawAttributePointerStage()
	{
			bool continueFlag = true;
			{

				// 颜色选择
				if (continueFlag && msg.message == WM_LBUTTONDOWN)
				{
					shared_ptr<BarUiShapeClass> activeSwatch;
					for (int i = static_cast<int>(
						BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(
							BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
					{
						auto swatch = shapeMap[static_cast<BarUISetShapeEnum>(i)];
						if (swatch && swatch->IsClick(
							msg.x, msg.y, barStyle.zoom))
						{
							activeSwatch = swatch;
							break;
						}
					}

					if (activeSwatch)
					{
						continueFlag = false;
						auto ApplySwatch = [&](const shared_ptr<BarUiShapeClass>& swatch)
							{
								SetPenColor(Inkeys::Color::SetAlphaR(
									swatch->fill.value().tar, 255));
								if (barState.drawAttributeBar.colorPickerOpen)
									ProjectCurrentColorPickerPoint();
								UpdateRendering();
							};
						ApplySwatch(activeSwatch);

						// 只有色块内的 Down 才启动拖选，穿过间隙后仍可切到新色块。
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (!msg.lbutton) break;

							for (int i = static_cast<int>(
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
								i <= static_cast<int>(
									BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
							{
								auto swatch = shapeMap[
									static_cast<BarUISetShapeEnum>(i)];
								if (!swatch || swatch == activeSwatch
									|| !swatch->IsClick(
										msg.x, msg.y, barStyle.zoom))
									continue;
								activeSwatch = swatch;
								ApplySwatch(activeSwatch);
								break;
							}
						}

						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(EM_MOUSE, floating_window);
					}
				}

				// 粗细预设和滑块固定入口
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
					bool thicknessPresetMode =
						PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect);
						for (const auto& button : thicknessButtons)
						{
							bool visible = thicknessPresetMode;
							auto obj = shapeMap[button.shape];
							if (!visible || !obj
								|| !obj->IsClick(msg.x, msg.y, barStyle.zoom))
								continue;

							continueFlag = false;
							if (msg.message == WM_LBUTTONDOWN)
							{
								bool clickCompleted = false;
								bool fineDialAtPress =
									barState.drawAttributeBar.thicknessViewMode
										== ThicknessViewMode::FineDial;
								bool sliderPinnedAtPress = button.presetIndex < 0
									&& barState.drawAttributeBar
										.thicknessSliderPinned;
								if (button.presetIndex < 0)
								{
									// 按下阶段只显示缩放/按压色，展开状态留到有效抬手再切换。
									barState.drawAttributeBar
										.thicknessSliderHover = false;
								}
								*button.pressed = true;
								StopIndependentHover(hoveredIndependentButton, true, true);
								hoveredIndependentButton =
									IndependentHoverTargetEnum::None;
								UpdateRendering(false);
								while (true)
								{
									if (!WaitForBarInteractionMessage(
										msg, EM_MOUSE, floating_window))
									{
										return BarInteractionStageResult::Shutdown;
									}
									if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
									{
										if (!msg.lbutton)
										{
											if (button.presetIndex >= 0)
											{
												if (fineDialAtPress)
													CancelThicknessFineDialSelection();
												SetPenWidth(static_cast<float>(
													GetBarThicknessPresetPx(
														stateMode.Pen.ModeSelect,
														button.presetIndex,
														barStyle.dpiZoom)));
											}
											else
											{
												if (fineDialAtPress)
												{
													auto thicknessSliderRange = GetBarThicknessSliderRange(
														stateMode.Pen.ModeSelect, barStyle.dpiZoom);
													bool fineDialConfirmationAllowed =
														stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
														&& thicknessSliderRange.supported
														&& barState.drawAttribute && !barState.fold
														&& barState.drawAttributeBar.thicknessViewMode
															== ThicknessViewMode::FineDial;
													if (fineDialConfirmationAllowed)
													{
														// 抬手时仍是有效 FineDial，才提交并允许 Popup 原地收尾。
														CommitThicknessFineDialSelection();
														CloseThicknessSlider(false);
														barState.drawAttributeBar
															.thicknessFineDialPopupExitLatchRequested = true;
													}
													else
													{
														// 生命周期收起可能先于合成抬手，避免请求残留到后续会话。
														barState.drawAttributeBar
															.thicknessFineDialPopupExitLatchRequested = false;
													}
												}
												else if (barUISet.TryBeginToggle(
													BarToggleChannel::ThicknessAdjust))
												{
													barState.drawAttributeBar
														.thicknessSliderPinned = !sliderPinnedAtPress;
													barState.drawAttributeBar.thicknessViewMode =
														sliderPinnedAtPress
															? ThicknessViewMode::Preview
															: ThicknessViewMode::Slider;
													barState.drawAttributeBar
														.thicknessSliderHover = false;
												}
											}
											clickCompleted = true;
											break;
										}
									}
									else break;
								}
								// 先合并抬手态与展开目标，再只唤醒一次，避免旋转早于缩放/颜色恢复。
								*button.pressed = false;
								UpdateRendering(clickCompleted && button.presetIndex >= 0);
								SuppressHoverUntilPointerMove();
									ClearBarInteractionMessages(
									EM_MOUSE, floating_window);
							}
						break;
					}
				}

				// 当前选中且支持标注线的笔型才拥有扩展菜单入口。
				if (continueFlag && barState.drawAttribute
					&& !barState.fold
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect))
				{
					auto extension = shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
					if (extension && extension->IsClick(
						msg.x, msg.y, barStyle.zoom))
					{
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN)
						{
							barState.drawAttributeBar.penTypeExtensionPress = true;
							StopIndependentHover(
								hoveredIndependentButton, true, true);
							hoveredIndependentButton =
								IndependentHoverTargetEnum::None;
							UpdateRendering(false);
							bool clickCompleted = false;
							while (true)
							{
								if (!WaitForBarInteractionMessage(
									msg, EM_MOUSE, floating_window))
								{
									return BarInteractionStageResult::Shutdown;
								}
								if (!extension->IsClick(
									msg.x, msg.y, barStyle.zoom)) break;
								if (!msg.lbutton)
								{
									clickCompleted = true;
									if (barState.drawAttributeBar.penTypeMenuOpen)
									{
										if (barUISet.TryBeginToggle(
											BarToggleChannel::PenTypeMenu))
											ClosePenTypeMenu();
									}
									else
									{
										bool directionLocked = barState.drawAttributeBar
											.penTypeMenuDirectionLocked;
										bool canResumeClosingMenu = directionLocked
											&& static_cast<bool>(barState.drawAttributeBar
												.penTypeMenuOpenBelow)
												== static_cast<bool>(
													barState.widgetPosition.primaryBar)
											&& barState.drawAttributeBar.penTypeMenuAnchorMode
												== static_cast<int>(stateMode.Pen.ModeSelect);
										if ((!directionLocked || canResumeClosingMenu)
											&& barUISet.TryBeginToggle(
												BarToggleChannel::PenTypeMenu))
										{
											// FineDial 在笔型菜单期间保持；实际切换时再取消旧候选。
											if (barState.drawAttributeBar.thicknessViewMode
												!= ThicknessViewMode::FineDial)
												CloseThicknessSlider(true);
											CloseThicknessOverflowTooltip();
											if (!directionLocked)
											{
												barState.drawAttributeBar
													.penTypeMenuOpenBelow =
														barState.widgetPosition.primaryBar;
												barState.drawAttributeBar
													.penTypeMenuAnchorMode = static_cast<int>(
														stateMode.Pen.ModeSelect);
												barState.drawAttributeBar
													.penTypeMenuDirectionLocked = true;
											}
											// 同方向快速反向时从当前进度续接。
											barState.drawAttributeBar.penTypeMenuOpen = true;
										}
									}
									break;
								}
							}
							barState.drawAttributeBar.penTypeExtensionPress = false;
							UpdateRendering(false);
							if (clickCompleted) SuppressHoverUntilPointerMove();
							ClearBarInteractionMessages(
								EM_MOUSE, floating_window);
						}
					}
				}

				// 激光笔
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Laser]; continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						while (true)
						{
							if (!WaitForBarInteractionMessage(msg, EM_MOUSE, floating_window))
								return BarInteractionStageResult::Shutdown;
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom) && !msg.lbutton)
							{
								ClosePenTypeMenu();
								stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
								stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
								stateMode.laserActive = true;
								SyncDraw3State();
								barButtonSet.UpdateDrawButtonStyle();
								UpdateRendering();
								break;
							}
							if (!obj->IsClick(msg.x, msg.y, barStyle.zoom)) break;
						}
						SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(EM_MOUSE, floating_window);
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
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
							if (!msg.lbutton)
							{
								if (stateMode.Pen.ModeSelect
									!= PenModeSelectEnum::IdtPenBrush1)
								{
									ClosePenTypeMenu();
									if (barState.drawAttributeBar.thicknessViewMode
										== ThicknessViewMode::FineDial)
										CancelThicknessFineDialSelection();
									stateMode.laserActive = false;
									stateMode.Pen.ModeSelect =
										PenModeSelectEnum::IdtPenBrush1;
									SyncDraw3State();
									barButtonSet.UpdateDrawButtonStyle();
									UpdateRendering();
								}

								break;
								}
							}
							else break;
						}
						barState.drawAttributeBar.brush1Press = false; UpdateRendering(false);
						SuppressHoverUntilPointerMove();

						ClearBarInteractionMessages(EM_MOUSE, floating_window);
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
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
							if (!msg.lbutton)
							{
								if (stateMode.Pen.ModeSelect
									!= PenModeSelectEnum::IdtPenHighlighter1)
								{
									ClosePenTypeMenu();
									if (barState.drawAttributeBar.thicknessViewMode
										== ThicknessViewMode::FineDial)
										CancelThicknessFineDialSelection();
									stateMode.laserActive = false;
									stateMode.Pen.ModeSelect =
										PenModeSelectEnum::IdtPenHighlighter1;
									SyncDraw3State();
									barButtonSet.UpdateDrawButtonStyle();
									UpdateRendering();
								}

								break;
								}
							}
							else break;
						}
						barState.drawAttributeBar.highlight1Press = false; UpdateRendering(false);
						SuppressHoverUntilPointerMove();

						ClearBarInteractionMessages(EM_MOUSE, floating_window);
					}
				}
			}
		return continueFlag
			? BarInteractionStageResult::PassThrough
			: BarInteractionStageResult::Consumed;
	}

	BarInteractionStageResult DispatchPointerStages()
	{
		// 浮层外点击关闭后仍按原顺序向下传递同一条消息。
		const auto stages = {
			&BarInteractionSession::HandleColorPickerPointerStage,
			&BarInteractionSession::HandleOverlayPointerStage,
			&BarInteractionSession::HandleMorePointerStage,
			&BarInteractionSession::HandleMainButtonAndBarPointerStage,
			&BarInteractionSession::HandleGeometryPointerStage,
			&BarInteractionSession::HandleThicknessAndFineDialPointerStage,
			&BarInteractionSession::HandleDrawAttributePointerStage,
		};
		for (const auto stage : stages)
		{
			const auto result = (this->*stage)();
			if (result != BarInteractionStageResult::PassThrough)
				return result;
		}
		return BarInteractionStageResult::PassThrough;
	}

public:
	void Run()
	{
		while (!offSignal)
		{
			const auto pollResult = PollInteractionMessage();
			if (pollResult == BarInteractionStageResult::Shutdown) break;
			if (pollResult == BarInteractionStageResult::Consumed) continue;

			const auto keyboardResult = HandleKeyboardMessage();
			if (keyboardResult == BarInteractionStageResult::Shutdown) break;
			if (keyboardResult == BarInteractionStageResult::Consumed) continue;

			const auto hoverResult = HandleCommonHoverAndOcclusion();
			if (hoverResult == BarInteractionStageResult::Shutdown) break;
			if (hoverResult == BarInteractionStageResult::Consumed) continue;

			const auto pointerResult = DispatchPointerStages();
			if (pointerResult == BarInteractionStageResult::Shutdown) break;
			if (pointerResult == BarInteractionStageResult::Consumed) continue;
		}
		(void)FinishInteraction();
	}

private:
	void UpdateRendering(bool updateState = true)
	{
		barUISet.UpdateRendering(updateState);
	}

	BarSeekResult Seek(const ExMessage& message)
	{
		ExMessage seekMessage = message;
		if (currentTouchScreenSample.ready
			&& IsBarTouchPointerMessage(message))
		{
			seekMessage.x = static_cast<short>(clamp<LONG>(
				currentTouchScreenSample.point.x, SHRT_MIN, SHRT_MAX));
			seekMessage.y = static_cast<short>(clamp<LONG>(
				currentTouchScreenSample.point.y, SHRT_MIN, SHRT_MAX));
			MarkBarTouchPointerMessage(seekMessage,
				IsBarTouchCancelMessage(message), true);
		}
		currentTouchScreenSample.ready = false;
		return (barUISet.*memberAccess.seek)(seekMessage);
	}

	void CloseThicknessOverflowTooltip()
	{
		(barUISet.*memberAccess.closeThicknessOverflowTooltip)();
	}

	void CloseDrawAttributeTooltips()
	{
		(barUISet.*memberAccess.closeDrawAttributeTooltips)();
	}

	void ClosePenTypeMenu()
	{
		(barUISet.*memberAccess.closePenTypeMenu)();
	}

	void CloseThicknessSlider(bool cancelCapture)
	{
		(barUISet.*memberAccess.closeThicknessSlider)(cancelCapture);
	}

	void CloseColorPicker(bool cancelCapture)
	{
		(barUISet.*memberAccess.closeColorPicker)(cancelCapture);
	}

	BarUISetClass& barUISet;
	BarButtonSetClass& barButtonSet;
	BarStateClass& barState;
	BarStyleClass& barStyle;
	ankerl::unordered_dense::map<BarUISetShapeEnum,
		shared_ptr<BarUiShapeClass>>& shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum,
		shared_ptr<BarUiSuperellipseClass>>& superellipseMap;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeBrushHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeHighlightHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributePenTypeExtensionHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributePenTypeFreeLineHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeThicknessFineHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeThicknessMediumHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeThicknessCoarseHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeThicknessAdjustHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeAnnotationCloseHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeOverflowCloseHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeColorPickerToneHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeColorPickerCloseHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& moreCloseHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& geometryStraightLineHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& geometryRectangleHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& geometryThicknessFineHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& geometryThicknessMediumHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& geometryThicknessCoarseHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& geometryCloseHoverStage;
	std::atomic<unsigned long long>& mainButtonClickPulseSerial;
	BarInteractionMemberAccess memberAccess;
	BarTouchScreenSample currentTouchScreenSample{};

	ExMessage msg{};
	BarButtonClass* lastClickedMainBarButton = nullptr;
	BarButtonClass* hoveredMainBarButton = nullptr;
	bool suppressHoverUntilPointerMove = false;
	POINT hoverSuppressionScreenPoint{};
	IndependentHoverTargetEnum hoveredIndependentButton =
		IndependentHoverTargetEnum::None;
	ThicknessFineDialPhase thicknessFineDialPhase =
		ThicknessFineDialPhase::Idle;
	array<ThicknessFineDialVelocitySample,
		BarThicknessFineDialVelocitySampleCount> thicknessFineDialSamples{};
	size_t thicknessFineDialSampleCount = 0;
	double thicknessFineDialRawValue = 0.0;
	double thicknessFineDialVisualValue = 0.0;
	double thicknessFineDialVelocity = 0.0;
	double thicknessFineDialResidualVelocity = 0.0;
	double thicknessFineDialSettleTarget = 0.0;
	double thicknessFineDialUnitTravelScreen = 1.0;
	int thicknessFineDialRangeMin = 0;
	int thicknessFineDialRangeMax = 0;
	ULONGLONG thicknessFineDialGrabTick = 0;
	chrono::steady_clock::time_point thicknessFineDialLastPhysicsTime =
		chrono::steady_clock::now();
	bool thicknessFineDialPhysicsClockNeedsReset = true;
	bool thicknessFineDialCommitIssued = false;
	// FineDial 隐藏 Slider 固定态，Popup 返回时恢复进入前的生命周期。
	bool thicknessFineDialReturnPinned = false;
};
}

// 鼠标交互
void BarUISetClass::Interact()
{
	const BarInteractionMemberAccess memberAccess
	{
		&BarUISetClass::Seek,
		&BarUISetClass::CloseThicknessOverflowTooltip,
		&BarUISetClass::CloseDrawAttributeTooltips,
		&BarUISetClass::ClosePenTypeMenu,
		&BarUISetClass::CloseThicknessSlider,
		&BarUISetClass::CloseColorPicker,
	};
	BarInteractionSession(
		*this, mainButtonClickPulseSerial, memberAccess).Run();
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
	if (!BarScreenToLayout(clientPoint)) return;
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
	if (!BarScreenToLayout(clientPoint)) return;
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

	// UI3 落笔时固定收起次级面板；主栏本身保持用户当前的展开状态。
	barState.drawAttribute = false;
	barState.geometryAttribute = false;
	barState.moreExpanded = false;
	ClosePenTypeMenu();
	CloseDrawAttributeTooltips();
	CloseThicknessSlider(true);
	CloseColorPicker(true);
	UpdateRendering(false);

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
	array<RECT, 6> nextRegions{};
	size_t nextCount = 0;
	const auto bottomDockSnapshot = BottomDockPresentedSnapshot();
	const double frameZoom = bottomDockSnapshot.zoom;
	auto AddShape = [&](const shared_ptr<BarUiShapeClass>& shape, bool body)
		{
			if (!shape || !shape->enable.val || shape->pct.val <= 0.000001
				|| nextCount >= nextRegions.size())
				return;
			RECT bounds = BarRenderingAttribute::GetWeigetRect(*shape, frameZoom);
			nextRegions[nextCount++] = body
				? TransformBarBottomDockBodyRect(
					bounds, bottomDockSnapshot.mapping, frameZoom)
				: Inkeys::UI::Bar::TranslateBarBottomDockRigidRect(bounds,
					bottomDockSnapshot.rigidTranslationDip, frameZoom);
		};
	auto AddSuperellipse = [&](const shared_ptr<BarUiSuperellipseClass>& superellipse)
		{
			if (!superellipse || !superellipse->enable.val
				|| superellipse->pct.val <= 0.000001 || nextCount >= nextRegions.size())
				return;
			RECT bounds = BarRenderingAttribute::GetWeigetRect(
				*superellipse, frameZoom);
			nextRegions[nextCount++] = TransformBarBottomDockBodyRect(
				bounds, bottomDockSnapshot.mapping, frameZoom);
		};

	AddSuperellipse(superellipseMap[BarUISetSuperellipseEnum::MainButton]);
	AddShape(shapeMap[BarUISetShapeEnum::MainBar], true);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar], false);
	AddShape(shapeMap[BarUISetShapeEnum::GeometryAttributeBar], false);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel], false);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble], false);

	// 距离判断统一在屏幕坐标完成，避免接受区内外分别换算客户区坐标。
	POINT clientOrigin = bottomDockSnapshot.monitorOrigin;
	const POINT directTranslation = bottomDockSnapshot.directTranslation;
	clientOrigin.x += directTranslation.x;
	clientOrigin.y += directTranslation.y;
	if (!floating_window)
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
	array<RECT, 6> visibleRegions{};
	size_t visibleRegionCount = 0;
	{
		lock_guard lock(borderCursorLightMutex);
		visibleRegions = borderCursorVisibleRegions;
		visibleRegionCount = borderCursorVisibleRegionCount;
	}

	const double zoom = BottomDockPresentedSnapshot().zoom;
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
BarSeekResult BarUISetClass::Seek(const ExMessage& msg)
{
	using namespace Inkeys::UI::Bar;
	BarSeekResult result;
	auto IsLeftButtonDown = []() -> bool
		{
			// HWND 移动后可能收不到抬手消息，拖动循环必须以系统实时按键态结束。
			return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		};
	const bool touchGesture = IsBarTouchPointerMessage(msg);
	if (touchGesture && IsBarTouchCancelMessage(msg))
	{
		result.allowClick = false;
		return result;
	}
	if ((touchGesture && (!msg.lbutton || IsBarTouchCancelMessage(msg)))
		|| (!touchGesture && !IsLeftButtonDown())) return result;

	auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
	if (!mainButton) return result;

	const BarPendingDisplaySnapshot initialDisplaySnapshot =
		PendingDisplaySnapshot();
	auto ResolveInteractionZoom = [&](const BarPendingDisplaySnapshot& snapshot)
		{
			return ResolveBarBottomDockInteractionZoom(snapshot.dpi,
				static_cast<double>(barStyle.configZoom));
		};
	// 吸收阶段不含 ULW；若恰好撞上，只等待这段极短的布局接管。
	for (;;)
	{
		BarDirectWindowDragPhase expected = BarDirectWindowDragPhase::Idle;
		if (directWindowDragPhase.compare_exchange_weak(
			expected, BarDirectWindowDragPhase::Dragging,
			memory_order_acq_rel, memory_order_acquire)) break;
		if (expected == BarDirectWindowDragPhase::Dragging) return result;
		lock_guard lock(directWindowDragMutex);
	}
	auto FinishDirectWindowDrag = [&]()
		{
			directWindowDragPhase.store(
				BarDirectWindowDragPhase::Idle, memory_order_release);
		};

	const BarBottomDockPresentedSnapshot initialPresentedSnapshot =
		BottomDockPresentedSnapshot();
	const double presentedZoom = initialPresentedSnapshot.zoom;
	double interactionZoom = presentedZoom;
	double maximumInteractionZoom = interactionZoom;
	const double initialTargetZoom =
		ResolveInteractionZoom(initialDisplaySnapshot);
	const bool initialDisplayTransitionPending =
		initialDisplaySnapshot.serial
			!= initialPresentedSnapshot.displaySerial
		|| abs(initialTargetZoom - presentedZoom) > 0.000001;
	const POINT initialMonitorOrigin = initialPresentedSnapshot.monitorOrigin;
	const POINT initialPresentedTranslation =
		initialPresentedSnapshot.directTranslation;
	const BarBottomDockMode initialMode = initialPresentedSnapshot.mode;
	bool whiteboardDockLocked = WhiteboardDockLockActive();
	const BarBottomDockPhase initialPhase = initialPresentedSnapshot.phase;
	const bool initialRecoveryActive =
		initialPresentedSnapshot.recoveryActive;
	const double initialPresentedElasticDip =
		initialPresentedSnapshot.elasticOffsetDip;
	const auto deferredTransitionBeforeGesture =
		bottomDockDeferredTransitionSerial.load(memory_order_acquire);
	const bool presentedStateRebaseRequired =
		initialDisplayTransitionPending
		|| deferredTransitionBeforeGesture
			> initialPresentedSnapshot.transitionSerial
		|| bottomDockMode.load(memory_order_acquire) != initialMode
		|| bottomDockPhase.load(memory_order_acquire) != initialPhase
		|| bottomDockRecoveryActive.load(memory_order_acquire)
			!= initialRecoveryActive
		|| abs(bottomDockElasticOffsetDip.load(memory_order_acquire)
			- initialPresentedElasticDip) > 0.000001;
	unsigned long long gestureRebaseSerial = 0;
	if (presentedStateRebaseRequired)
	{
		// 上一手势可能尚未提交 ULW；新手势先撤回到同一份已呈现 tuple。
		bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel);
		bottomDockMode.store(initialMode, memory_order_relaxed);
		bottomDockPhase.store(initialPhase, memory_order_relaxed);
		bottomDockRecoveryActive.store(
			initialRecoveryActive, memory_order_relaxed);
		bottomDockElasticOffsetDip.store(
			initialPresentedElasticDip, memory_order_relaxed);
		directWindowDragTranslationX.store(
			initialPresentedTranslation.x, memory_order_relaxed);
		directWindowDragTranslationY.store(
			initialPresentedTranslation.y, memory_order_relaxed);
		gestureRebaseSerial = bottomDockTransitionSerial.fetch_add(
			1, memory_order_acq_rel) + 1;
		bottomDockDeferredTransitionSerial.store(
			gestureRebaseSerial, memory_order_release);
	}
	double baseMainCenterScreenX =
		initialPresentedSnapshot.mainCenterScreenX
		- initialPresentedTranslation.x;
	double baseMainCenterScreenY =
		initialPresentedSnapshot.mainCenterScreenY
		- initialPresentedTranslation.y;
	LONG appliedDeltaX = directWindowDragTranslationX.load(memory_order_acquire);
	LONG appliedDeltaY = directWindowDragTranslationY.load(memory_order_acquire);
	bool directMoveFailed = false;

	BarBottomDockEnvironment environment{
		initialPresentedSnapshot.monitorBounds,
		initialPresentedSnapshot.workArea,
		interactionZoom };
	unsigned long long observedDisplaySerial =
		initialPresentedSnapshot.displaySerial;
	bool displayEnvironmentAwaited = initialDisplayTransitionPending;
	unsigned long long requestedDisplaySerial = initialDisplaySnapshot.serial;
	double requestedDisplayZoom = initialTargetZoom;
	const double bodyHeightDip = 80.0;
	const double strokeWidthDip = mainButton->ft.has_value()
		? max(0.0, static_cast<double>(mainButton->ft.value().tar)) : 0.0;
	const double visibleHalfWidthDip = mainButton->GetW() / 2.0
		+ strokeWidthDip / 2.0;
	auto VisibleHalfHeightScreen = [&]()
		{
			return (bodyHeightDip + strokeWidthDip)
				* interactionZoom / 2.0;
		};

	POINT startPointer{};
	if (touchGesture)
	{
		if (IsBarTouchScreenMessage(msg))
			startPointer = POINT{ msg.x, msg.y };
		else startPointer = POINT{
			msg.x + initialMonitorOrigin.x + initialPresentedTranslation.x,
			msg.y + initialMonitorOrigin.y + initialPresentedTranslation.y };
	}
	else if (!GetCursorPos(&startPointer))
	{
		// 初始采样失败等价于取消手势，不能把默认结果误当成一次点击。
		result.allowClick = false;
		FinishDirectWindowDrag();
		if (!offSignal) UpdateRendering(false);
		return result;
	}
	POINT previousPointer = startPointer;
	const double actualMainCenterScreenX =
		initialPresentedSnapshot.mainCenterScreenX;
	const double actualMainCenterScreenY =
		initialPresentedSnapshot.mainCenterScreenY;
	double grabOffsetScreenX = startPointer.x - actualMainCenterScreenX;
	double grabOffsetScreenY = startPointer.y
		- (actualMainCenterScreenY
			+ initialPresentedSnapshot.rigidTranslationDip * presentedZoom);
	bottomDockDragRigidGripScreenY.store(
		startPointer.y - grabOffsetScreenY, memory_order_release);
	const double initialFloatingVisibleBottomScreenY =
		startPointer.y - grabOffsetScreenY + VisibleHalfHeightScreen();
	const double initialDockCenterScreenY = ResolveBarBottomDockCenterScreenY(
		ResolveBarBottomDockLine(
			environment.monitorBounds, environment.workArea),
		bodyHeightDip, strokeWidthDip, interactionZoom);
	BarBottomDockDragTracker dockTracker;
	dockTracker.Begin(initialMode, startPointer.y,
		initialFloatingVisibleBottomScreenY,
		initialDockCenterScreenY + grabOffsetScreenY, environment);
	bottomDockDragActive.store(true, memory_order_release);
	bottomDockPhase.store(initialMode == BarBottomDockMode::BottomDocked
		? BarBottomDockPhase::Dragging : initialPhase, memory_order_release);
	if (initialMode == BarBottomDockMode::BottomDocked
		|| presentedStateRebaseRequired)
		bottomDockElasticOffsetDip.store(
			initialPresentedElasticDip, memory_order_release);
	UpdateRendering(false);

	double maximumElasticTravelScreen = 0.0;
	double lastPublishedElasticDip = initialPresentedElasticDip;
	bool downwardDetachSeen = false;
	bool downwardDetachBlockedAtRelease = false;
	bool gestureCancelled = false;
	unsigned long long awaitedTransitionSerial = gestureRebaseSerial;
	auto PublishPresentationBarrier = [&]()
		{
			// 缩放或显示参数改变时，先让 ULW 提交新尺寸，再允许直移旧位图。
			bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel);
			const auto barrierSerial = bottomDockTransitionSerial.fetch_add(
				1, memory_order_acq_rel) + 1;
			bottomDockDeferredTransitionSerial.store(
				barrierSerial, memory_order_release);
			awaitedTransitionSerial = barrierSerial;
			UpdateRendering(false);
		};
	struct DockPublication
	{
		bool visualChanged = false;
		unsigned long long transitionSerial = 0;
	};
	auto PublishDockUpdate = [&](const BarBottomDockDragUpdate& update)
		{
			DockPublication publication;
			publication.visualChanged = update.modeChanged
				|| abs(update.elasticOffsetDip - lastPublishedElasticDip) > 0.000001;
			bottomDockMode.store(update.mode, memory_order_release);
			bottomDockPhase.store(update.phase, memory_order_release);
			bottomDockElasticOffsetDip.store(
				update.elasticOffsetDip, memory_order_release);
			if (update.detached)
			{
				bottomDockRecoveryActive.store(true, memory_order_release);
				downwardDetachSeen = update.elasticOffsetDip > 0.0;
			}
			else if (update.captured)
			{
				bottomDockRecoveryActive.store(false, memory_order_release);
				downwardDetachSeen = false;
			}
			if (update.modeChanged)
			{
				result.captured = result.captured || update.captured;
				result.detached = result.detached || update.detached;
				result.modeChanged = true;
				result.allowClick = false;
				// 偶数 serial 发布在模式、阶段和形变量之后，渲染帧可据此建立呈现屏障。
				publication.transitionSerial =
					bottomDockTransitionSerial.fetch_add(
						1, memory_order_acq_rel) + 1;
			}
			maximumElasticTravelScreen = max(maximumElasticTravelScreen,
				abs(update.elasticOffsetDip - initialPresentedElasticDip)
					* interactionZoom);
			lastPublishedElasticDip = update.elasticOffsetDip;
			return publication;
		};

	auto ApplyAbsolutePointer = [&](POINT pointer)
		{
			const double stepX = static_cast<double>(pointer.x - previousPointer.x);
			const double stepY = static_cast<double>(pointer.y - previousPointer.y);
			const double stepLength = hypot(stepX, stepY);
			result.rawPathLength += stepLength;
			result.moved = result.moved || stepLength > 0.0;
			previousPointer = pointer;
			auto RebasePointerToPresented = [&](const auto& presented)
				{
					baseMainCenterScreenX = presented.mainCenterScreenX
						- presented.directTranslation.x;
					baseMainCenterScreenY = presented.mainCenterScreenY
						- presented.directTranslation.y;
					grabOffsetScreenX = pointer.x - presented.mainCenterScreenX;
					grabOffsetScreenY = pointer.y
						- (presented.mainCenterScreenY
							+ presented.rigidTranslationDip * presented.zoom);
					const double floatingBottom = pointer.y - grabOffsetScreenY
						+ VisibleHalfHeightScreen();
					const double dockCenter = ResolveBarBottomDockCenterScreenY(
						ResolveBarBottomDockLine(
							environment.monitorBounds, environment.workArea),
						bodyHeightDip, strokeWidthDip, interactionZoom);
					dockTracker.RebaseDockGrip(
						dockCenter + grabOffsetScreenY, floatingBottom);
					appliedDeltaX = directWindowDragTranslationX.load(
						memory_order_acquire);
					appliedDeltaY = directWindowDragTranslationY.load(
						memory_order_acquire);
				};
			auto AdoptPresentedEnvironment = [&](const auto& presented)
				{
					interactionZoom = presented.zoom;
					maximumInteractionZoom = max(
						maximumInteractionZoom, interactionZoom);
					environment = BarBottomDockEnvironment{
						presented.monitorBounds,
						presented.workArea,
						interactionZoom };
					observedDisplaySerial = presented.displaySerial;
					requestedDisplaySerial = presented.displaySerial;
					requestedDisplayZoom = interactionZoom;
				};

			const auto presented = BottomDockPresentedSnapshot();
			const bool presentedEnvironmentChanged =
				presented.displaySerial != observedDisplaySerial
				|| abs(presented.zoom - interactionZoom) > 0.000001;
			bool pointerRebasedToPresented = false;
			if (presentedEnvironmentChanged)
			{
				// 真实上屏的环境立即接管阈值；形态 barrier 可继续等待自己的 serial。
				AdoptPresentedEnvironment(presented);
				RebasePointerToPresented(presented);
				pointerRebasedToPresented = true;
				displayEnvironmentAwaited = false;
			}
			if (awaitedTransitionSerial != 0
				&& presented.transitionSerial >= awaitedTransitionSerial)
			{
				if (displayEnvironmentAwaited)
					AdoptPresentedEnvironment(presented);
				// 对应形态/位移已经接管 HWND，吸收新基准后释放 barrier。
				if (!pointerRebasedToPresented)
					RebasePointerToPresented(presented);
				awaitedTransitionSerial = 0;
				displayEnvironmentAwaited = false;
			}
			const BarPendingDisplaySnapshot latestDisplaySnapshot =
				PendingDisplaySnapshot();
			const double latestZoom = ResolveInteractionZoom(
				latestDisplaySnapshot);
			const unsigned long long comparisonSerial = displayEnvironmentAwaited
				? requestedDisplaySerial : observedDisplaySerial;
			const double comparisonZoom = displayEnvironmentAwaited
				? requestedDisplayZoom : interactionZoom;
			if (latestDisplaySnapshot.serial != comparisonSerial
				|| abs(latestZoom - comparisonZoom) > 0.000001)
			{
				// 等待期间只为真正的新目标建立屏障，连续采样不会重复推进 serial。
				requestedDisplaySerial = latestDisplaySnapshot.serial;
				requestedDisplayZoom = latestZoom;
				displayEnvironmentAwaited = true;
				bottomDockDragRigidGripScreenY.store(
					pointer.y - grabOffsetScreenY, memory_order_release);
				PublishPresentationBarrier();
			}
			bottomDockDragRigidGripScreenY.store(
				pointer.y - grabOffsetScreenY, memory_order_release);

			const double floatingVisibleBottomScreenY =
				pointer.y - grabOffsetScreenY + VisibleHalfHeightScreen();
			const BarBottomDockDragUpdate dockUpdate = dockTracker.Update(
				pointer.y, floatingVisibleBottomScreenY, environment);
			if (whiteboardDockLocked && dockUpdate.modeChanged)
			{
				// 手动离开或重新捕获底栏后即恢复普通拖动，不再保留专用横向锁。
				ClearWhiteboardDockLock();
				whiteboardDockLocked = false;
			}

			double desiredMainCenterScreenX = whiteboardDockLocked
				? actualMainCenterScreenX : pointer.x - grabOffsetScreenX;
			desiredMainCenterScreenX = ClampBarBottomDockMainCenterScreenX(
				desiredMainCenterScreenX, environment.monitorBounds,
				visibleHalfWidthDip, interactionZoom);
			double desiredMainCenterScreenY = 0.0;
			if (dockUpdate.mode == BarBottomDockMode::BottomDocked)
				desiredMainCenterScreenY =
					dockUpdate.constrainedGripScreenY - grabOffsetScreenY;
			else
			{
				const double rawMainCenterScreenY =
					pointer.y - grabOffsetScreenY;
				desiredMainCenterScreenY = rawMainCenterScreenY;
				const double minimumY = environment.monitorBounds.top
					+ VisibleHalfHeightScreen();
				const double maximumY = max(minimumY,
					static_cast<double>(environment.monitorBounds.bottom)
						- VisibleHalfHeightScreen());
				desiredMainCenterScreenY = clamp(
					desiredMainCenterScreenY, minimumY, maximumY);
				const double dockCenterScreenY = ResolveBarBottomDockCenterScreenY(
					ResolveBarBottomDockLine(environment.monitorBounds,
						environment.workArea), bodyHeightDip,
					strokeWidthDip, interactionZoom);
				downwardDetachBlockedAtRelease =
					ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
						downwardDetachSeen, rawMainCenterScreenY,
						maximumY, dockCenterScreenY);
			}
			if (dockUpdate.mode == BarBottomDockMode::BottomDocked)
				downwardDetachBlockedAtRelease = false;

			const LONG pixelDeltaX = static_cast<LONG>(lround(
				desiredMainCenterScreenX - baseMainCenterScreenX));
			const LONG pixelDeltaY = static_cast<LONG>(lround(
				desiredMainCenterScreenY - baseMainCenterScreenY));
			const bool translationChanged = pixelDeltaX != appliedDeltaX
				|| pixelDeltaY != appliedDeltaY;
			if (dockUpdate.modeChanged)
			{
				// 奇数 serial 先封住帧快照，随后位移与形态作为一个转换发布。
				bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel);
			}
			if (translationChanged)
			{
				directWindowDragTranslationX.store(
					pixelDeltaX, memory_order_release);
				directWindowDragTranslationY.store(
					pixelDeltaY, memory_order_release);
			}
			const DockPublication publication = PublishDockUpdate(dockUpdate);
			if (dockUpdate.modeChanged)
			{
				// 捕获/脱离必须先由 ULW 同帧提交新位图和新位置，
				// 在此之前禁止当前或后续采样用 SetWindowPos 移动上一帧位图。
				bottomDockDeferredTransitionSerial.store(
					publication.transitionSerial, memory_order_release);
				awaitedTransitionSerial = publication.transitionSerial;
			}
			bool requestRendering = publication.visualChanged;
			if (translationChanged)
			{
				const bool deferDirectMove = awaitedTransitionSerial != 0;
				if (!deferDirectMove)
				{
					unique_lock lock(directWindowDragMutex, try_to_lock);
					if (lock.owns_lock())
					{
						RECT currentWindowRect{};
						if (committedWindowScreenBoundsReady)
							currentWindowRect = committedWindowScreenBounds;
						else if (!GetWindowRect(floating_window, &currentWindowRect))
						{
							directMoveFailed = true;
							return false;
						}
						const POINT desiredTranslation{ pixelDeltaX, pixelDeltaY };
						const POINT presentedTranslation{
							directWindowPresentedTranslationX.load(memory_order_acquire),
							directWindowPresentedTranslationY.load(memory_order_acquire) };
						const POINT moveDelta = ResolveBarDirectWindowMoveDelta(
							desiredTranslation, presentedTranslation);
						if ((moveDelta.x != 0 || moveDelta.y != 0)
							&& !SetWindowPos(floating_window, nullptr,
								currentWindowRect.left + moveDelta.x,
								currentWindowRect.top + moveDelta.y, 0, 0,
								SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE))
						{
							directWindowDragTranslationX.store(
								appliedDeltaX, memory_order_release);
							directWindowDragTranslationY.store(
								appliedDeltaY, memory_order_release);
							directMoveFailed = true;
							return false;
						}
						if (moveDelta.x != 0 || moveDelta.y != 0)
						{
							committedWindowScreenBounds = TranslateBarWindowRect(
								currentWindowRect, moveDelta);
							committedWindowScreenBoundsReady = true;
							RebaseBottomDockPresentedWindow(
								desiredTranslation, moveDelta);
							// 第三光源接受区是屏幕缓存，HWND 直移后同步平移，不能等下一次 ULW。
							lock_guard lightLock(borderCursorLightMutex);
							for (size_t regionIndex = 0;
								regionIndex < borderCursorVisibleRegionCount;
								++regionIndex)
							{
								RECT& region = borderCursorVisibleRegions[regionIndex];
								region.left += moveDelta.x;
								region.right += moveDelta.x;
								region.top += moveDelta.y;
								region.bottom += moveDelta.y;
							}
						}
					}
					else requestRendering = true;
				}
				else requestRendering = true;
				appliedDeltaX = pixelDeltaX;
				appliedDeltaY = pixelDeltaY;
			}
			if (requestRendering) UpdateRendering(false);
			return translationChanged || publication.visualChanged;
		};

	if (touchGesture)
	{
		ExMessage touchMessage = msg;
		while (!offSignal && !directMoveFailed)
		{
			if (!WaitForBarInteractionMessage(
				touchMessage, EM_MOUSE, floating_window, true))
			{
				gestureCancelled = true;
				break;
			}
			if (!IsBarTouchPointerMessage(touchMessage)) continue;
			if (IsBarTouchCancelMessage(touchMessage))
			{
				gestureCancelled = true;
				break;
			}

			POINT pointer{};
			if (IsBarTouchScreenMessage(touchMessage))
				pointer = POINT{ touchMessage.x, touchMessage.y };
			else pointer = POINT{
				touchMessage.x + initialMonitorOrigin.x
					+ initialPresentedTranslation.x,
				touchMessage.y + initialMonitorOrigin.y
					+ initialPresentedTranslation.y };
			(void)ApplyAbsolutePointer(pointer);
			if (!touchMessage.lbutton
				|| touchMessage.message == WM_LBUTTONUP) break;
		}
	}
	else
	{
		POINT point = startPointer;
		while (!offSignal && !directMoveFailed)
		{
			if (!IsLeftButtonDown()) break;
			if (!GetCursorPos(&point))
			{
				gestureCancelled = true;
				break;
			}
			if (!ApplyAbsolutePointer(point) && !directMoveFailed)
				this_thread::sleep_for(chrono::milliseconds(15));
		}
	}

	if (directMoveFailed)
	{
		const bool rollbackModeChanged = bottomDockMode.load(
			memory_order_acquire) != initialMode;
		if (rollbackModeChanged)
			bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel);
		bottomDockMode.store(initialMode, memory_order_release);
		bottomDockPhase.store(initialPhase, memory_order_release);
		bottomDockElasticOffsetDip.store(
			initialPresentedElasticDip, memory_order_release);
		bottomDockRecoveryActive.store(
			initialRecoveryActive, memory_order_release);
		if (rollbackModeChanged)
		{
			const auto rollbackSerial = bottomDockTransitionSerial.fetch_add(
				1, memory_order_acq_rel) + 1;
			bottomDockDeferredTransitionSerial.store(
				rollbackSerial, memory_order_release);
		}
	}
	else
	{
		dockTracker.End();
		const BarBottomDockMode finalMode = downwardDetachBlockedAtRelease
			? BarBottomDockMode::BottomDocked : dockTracker.Mode();
		const double currentVisualOffset =
			BottomDockPresentedSnapshot().elasticOffsetDip;
		const bool needsRecovery = abs(currentVisualOffset)
			> BarBottomDockSettleDistanceDip
			|| abs(dockTracker.ElasticOffsetDip())
				> BarBottomDockSettleDistanceDip;
		const bool releaseModeChanged = bottomDockMode.load(
			memory_order_acquire) != finalMode;
		if (releaseModeChanged)
			bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel);
		bottomDockMode.store(finalMode, memory_order_release);
		bottomDockPhase.store(needsRecovery
			? BarBottomDockPhase::Recovering
			: BarBottomDockPhase::Stable, memory_order_release);
		bottomDockElasticOffsetDip.store(needsRecovery
			? dockTracker.ElasticOffsetDip() : 0.0, memory_order_release);
		bottomDockRecoveryActive.store(
			finalMode == BarBottomDockMode::Floating && needsRecovery,
			memory_order_release);
		if (releaseModeChanged)
		{
			const auto releaseSerial = bottomDockTransitionSerial.fetch_add(
				1, memory_order_acq_rel) + 1;
			bottomDockDeferredTransitionSerial.store(
				releaseSerial, memory_order_release);
		}
	}
	bottomDockDragActive.store(false, memory_order_release);
	FinishDirectWindowDrag();
	// 松手只发布接管请求；布局值由渲染线程吸收，避免和动画线程并发写对象。
	if (!offSignal) UpdateRendering(false);
	result.allowClick = ShouldAllowBarBottomDockClick(
		directMoveFailed, gestureCancelled, result.modeChanged,
		result.rawPathLength, maximumElasticTravelScreen,
		maximumInteractionZoom);
	return result;
}

namespace Inkeys::UI::Bar
{
	Inkeys::Message::Reply QueueWindowMessageInLayoutSpace(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const auto extraInfo = static_cast<ULONG_PTR>(GetMessageExtraInfo());
		if (Inkeys::Message::IsPointerGeneratedMouseMessage(
			message, extraInfo))
			return { Inkeys::Message::Action::Discard, 0 };
		if (!IsBarCoordinateMessage(message)) return {};

		// 在窗口线程入队时就固化屏幕坐标，避免 resize 后用新原点解释旧 client 消息。
		POINT point{};
		if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
		{
			point.x = GET_X_LPARAM(lParam);
			point.y = GET_Y_LPARAM(lParam);
		}
		else
		{
			const LPARAM messagePosition = static_cast<LPARAM>(GetMessagePos());
			point.x = GET_X_LPARAM(messagePosition);
			point.y = GET_Y_LPARAM(messagePosition);
		}
		BarScreenToLayout(point);

		ExMessage queued{};
		queued.message = static_cast<USHORT>(message);
		queued.x = static_cast<short>(clamp<LONG>(point.x, SHRT_MIN, SHRT_MAX));
		queued.y = static_cast<short>(clamp<LONG>(point.y, SHRT_MIN, SHRT_MAX));
		queued.wheel = message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL
			? GET_WHEEL_DELTA_WPARAM(wParam) : 0;
		const WORD keyState = GET_KEYSTATE_WPARAM(wParam);
		queued.shift = (keyState & MK_SHIFT) != 0;
		queued.ctrl = (keyState & MK_CONTROL) != 0;
		queued.lbutton = (keyState & MK_LBUTTON) != 0;
		queued.mbutton = (keyState & MK_MBUTTON) != 0;
		queued.rbutton = (keyState & MK_RBUTTON) != 0;
		(void)Inkeys::Window::Enqueue(hwnd, queued);
		return { Inkeys::Message::Action::Discard, 0 };
	}

	bool TryQueueColorPickerKeyboardInput(BYTE vkCode, bool keyDown)
	{
		bool movementKey = vkCode == VK_LEFT || vkCode == VK_RIGHT
			|| vkCode == VK_UP || vkCode == VK_DOWN
			|| vkCode == 'A' || vkCode == 'D'
			|| vkCode == 'W' || vkCode == 'S';
		if (!movementKey || offSignal || !floating_window
			|| !IsWindow(floating_window)
			|| stateMode.StateModeSelect != StateModeSelectEnum::IdtPen
			|| barUISet.barState.fold || !barUISet.barState.drawAttribute
			|| !barUISet.barState.drawAttributeBar.colorPickerOpen)
			return false;

		ExMessage message{};
		message.message = keyDown ? WM_KEYDOWN : WM_KEYUP;
		message.vkcode = vkCode;
		return Inkeys::Window::Enqueue(floating_window, message);
	}

	void NotifyCanvasDrawingStarted()
	{
		// 只在首个并发笔迹进入时通知窗口线程，避免每个采样或多指笔迹重复切换状态。
		if (BarCanvasDrawingActivityCount.fetch_add(1, std::memory_order_acq_rel) == 0
			&& !offSignal && floating_window)
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
}
