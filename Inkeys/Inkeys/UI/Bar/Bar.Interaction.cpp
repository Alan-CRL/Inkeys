module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Business/LegacyDrawState.hpp"
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../Drawing/Draw3/Draw3.Shortcut.h"
#include "../../Input/KeyboardShortcuts.h"
#include <deque>
#include <cstdio>
#include <d2d1helper.h>
#include "../../Window/Window.Legacy.hpp"
#include "Bar.BottomDock.h"
#include "Bar.AutoCollapse.h"
#include "Bar.AutoCollapseInteraction.h"
#include "../../Input/UiPointerInput.h"
#include "../../Window/Window.MessageDispatch.h"
#include <optional>
#include "Bar.I18nFormat.h"
#include "Bar.WindowGeometry.h"
#include "Bar.PresentationProbe.h"
#include "Bar.Presentation.Source.h"

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.UI.Bar;
import :Main;
import :Layout;
import :Atomic;
import :Theme;

import Inkeys.Conv.Color;
import Inkeys.Message;
import Inkeys.Other.Inputs;
import Inkeys.Other.Config;
import Inkeys.Input.MouseHook;
import Inkeys.UI.Setting;
import Inkeys.UI.RenderPipeline;
import Inkeys.Window;
import Inkeys.Display;
import Inkeys.UI.MessageBox;
import Inkeys.UI.Freeze;
import Inkeys.Startup.Progress;
import Inkeys.Drawing.Draw3.diagnostics;
using Inkeys::UI::Bar::BarToggleChannel;
using Inkeys::UI::Bar::SetBarButtonPressedVisual;
using Inkeys::UI::Bar::StartBarButtonHoverVisual;
using Inkeys::UI::Bar::StopBarButtonHoverVisual;
#include "Bar.KeyboardShortcuts.h"

int Inkeys::UI::Bar::RunKeyboardShortcutBusinessProductionTest() noexcept
{
	return BarKeyboardShortcutOwner<BarShortcutTestBackend>::ProductionTest();
}
constexpr ULONGLONG BarBorderCursorGraceDurationMs = 5000;
constexpr UINT_PTR BarBorderCursorGraceTimerId = 0x494B4301;
constexpr UINT BarThicknessTooltipHoverGraceMs = 100;
constexpr UINT_PTR BarThicknessAnnotationTooltipGraceTimerId = 0x494B4302;
constexpr UINT_PTR BarThicknessOverflowTooltipGraceTimerId = 0x494B4303;
constexpr UINT_PTR BarRecallChromeIdleTimerId = 0x494B4304;
constexpr UINT BarBorderCursorSuspendMessage = WM_APP + 0x31;
constexpr UINT BarCanvasDrawingActivityMessage = WM_APP + 0x32;
constexpr UINT BarThicknessSliderCaptureMessage = WM_APP + 0x33;
constexpr UINT BarColorPickerCaptureMessage = WM_APP + 0x34;
constexpr UINT BarBorderCursorSurfacePointerMessage = WM_APP + 0x35;
constexpr short BarTouchPointerMessageMarker = SHRT_MIN;
constexpr short BarTouchCancelMessageMarker = SHRT_MIN + 1;
constexpr short BarTouchDirectDragScreenMessageMarker = SHRT_MIN + 2;
constexpr short BarTouchCancelScreenMessageMarker = SHRT_MIN + 3;
constexpr short BarPointerCancelMessageMarker = SHRT_MIN + 4;
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
std::atomic_uint64_t BarAutoCollapseNativeDownSerial{ 0 };
// 消息线程拥有入口按压态，渲染线程只通过只读接口取得缩放目标。
IdtAtomic<bool> BarColorPickerEntryPressed = false;

namespace
{
	constexpr std::array<Inkeys::Window::WindowRole, 4>
		BorderCursorSurfaceRoles{
			Inkeys::Window::WindowRole::PptBottomLeft,
			Inkeys::Window::WindowRole::PptBottomRight,
			Inkeys::Window::WindowRole::PptMiddleLeft,
			Inkeys::Window::WindowRole::PptMiddleRight,
		};
	std::mutex borderCursorSurfaceRegionMutex;
	std::array<RECT, BorderCursorSurfaceRoles.size()>
		borderCursorSurfaceRegions{};
	std::array<bool, BorderCursorSurfaceRoles.size()>
		borderCursorSurfaceRegionVisible{};

	struct BorderCursorSurfaceRegionSnapshot
	{
		std::array<RECT, BorderCursorSurfaceRoles.size()> bounds{};
		std::array<bool, BorderCursorSurfaceRoles.size()> visible{};
	};

	[[nodiscard]] BorderCursorSurfaceRegionSnapshot
		SnapshotBorderCursorSurfaceRegions() noexcept
	{
		std::lock_guard lock(borderCursorSurfaceRegionMutex);
		return { borderCursorSurfaceRegions,
			borderCursorSurfaceRegionVisible };
	}

	[[nodiscard]] bool IsBorderCursorAcceptingWindow(
		HWND barWindow, HWND candidate) noexcept
	{
		if (!candidate) return false;
		if (candidate == barWindow) return true;
		auto& service = Inkeys::Window::GetService();
		for (const auto role : BorderCursorSurfaceRoles)
			if (candidate == service.Handle(role)) return true;
		return false;
	}
}

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
	if (message.message != WM_MOUSEMOVE && message.message != WM_LBUTTONDOWN
		&& message.message != WM_LBUTTONUP && message.message != WM_LBUTTONDBLCLK)
		return false;
	const auto marker = static_cast<unsigned short>(message.wheel);
	if (message.message != WM_MOUSEWHEEL && message.message != WM_MOUSEHWHEEL
		&& (marker & 0x8000u) && (marker & 0x3fffu) >= 16) return true;
	return message.message != WM_MOUSEWHEEL
		&& (message.wheel == BarTouchPointerMessageMarker
			|| message.wheel == BarTouchCancelMessageMarker
			|| message.wheel == BarTouchDirectDragScreenMessageMarker
			|| message.wheel == BarTouchCancelScreenMessageMarker);
}

bool IsBarTouchCancelMessage(const ExMessage& message)
{
	if (message.message != WM_MOUSEMOVE && message.message != WM_LBUTTONDOWN
		&& message.message != WM_LBUTTONUP && message.message != WM_LBUTTONDBLCLK)
		return false;
	const auto marker = static_cast<unsigned short>(message.wheel);
	if (message.message != WM_MOUSEWHEEL && message.message != WM_MOUSEHWHEEL
		&& (marker & 0xc000u) == 0xc000u
		&& (marker & 0x3fffu) >= 16) return true;
	return message.message != WM_MOUSEWHEEL
		&& (message.wheel == BarTouchCancelMessageMarker
			|| message.wheel == BarTouchCancelScreenMessageMarker);
}

bool IsBarPointerCancelMessage(const ExMessage& message)
{
	return IsBarTouchCancelMessage(message)
		|| (message.message == WM_CANCELMODE
			&& message.wheel == BarPointerCancelMessageMarker);
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
	const POINT logicalPoint = barUISet.BottomDockRigidHitTestPoint(
		POINT{ message.x, message.y });
	message.y = static_cast<short>(clamp(
		static_cast<int>(logicalPoint.y),
		static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
	message.x = static_cast<short>(clamp(
		static_cast<int>(logicalPoint.x),
		static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
}

void ApplyBarBottomDockBodyHitTestFromRigid(
	ExMessage& message, int* visualY = nullptr, int* visualX = nullptr)
{
	if (!IsBarCoordinateMessage(message.message)
		|| IsBarTouchScreenMessage(message))
	{
		if (visualY) *visualY = message.y;
		if (visualX) *visualX = message.x;
		return;
	}
	POINT visualPoint{};
	const POINT logicalPoint = barUISet.BottomDockBodyHitTestPointFromRigid(
		POINT{ message.x, message.y }, &visualPoint);
	if (visualY) *visualY = visualPoint.y;
	if (visualX) *visualX = visualPoint.x;
	message.y = static_cast<short>(clamp(
		static_cast<int>(logicalPoint.y),
		static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
	message.x = static_cast<short>(clamp(
		static_cast<int>(logicalPoint.x),
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

namespace
{
	namespace Pointer = Inkeys::Input::UiPointer;
	Pointer::MouseChannel barPointerMouse;
	std::atomic_uint64_t barPointerCancelSerial{ 0 };
	std::atomic_uint64_t barPointerActiveOrdinal{ 0 };
	std::atomic_uint64_t barPointerRequestedCancel{ 0 };
	std::uint64_t barPointerNextOrdinal = 0;
	struct BarQueuedPointerSample
	{
		unsigned short token = 0;
		Pointer::Sample sample{};
		std::uint64_t cancelSerial = 0;
		Pointer::MouseGesture mouseAdmission;
	};
	std::mutex barPointerMetadataMutex;
	// 大于 4096 条 HiMsg 容量，且为跳过保留 token 后的回绕留余量，避免覆盖未出队样本。
	std::array<BarQueuedPointerSample, 8192> barPointerMetadata{};
	unsigned short barPointerNextToken = 16;
	struct BarTouchProducerState
	{
		HANDLE source = nullptr;
		DWORD id = 0;
		bool active = false;
		bool primary = false;
		POINT point{};
		std::uint64_t generation = 0;
	} barTouchProducer;
	std::uint64_t barTouchGeneration = 0;
	// 原生鼠标代次由窗口线程生成，迟到的 Up 不能结束下一次按下。
	std::uint64_t barNativeMouseGeneration = 0;
	std::uint64_t barNativeMouseActiveGeneration = 0;
	// 下列会话字段只由交互线程写；窗口线程仅发布 metadata / cancel serial。
	Pointer::Session barPointerSession;
	Pointer::MouseGesture barMouseGesture;
	Pointer::Sample barPointerLatest{};
	bool barPointerSampleReady = false;
	bool barMouseArmed = false;
	std::uint64_t barPointerModeRevision = 0;
	std::uint64_t barFixtureTouchGeneration = 0;
	bool barPointerFoldAtDown = false;
	std::uint64_t barPointerObservedCancel = 0;
	std::optional<BarQueuedPointerSample> barPointerPending;
	ExMessage barPointerPendingMessage{};

	// 只由交互线程记录已消费样本；不跨线程读取 producer 的可变状态。
	struct BarPointerDragDiagnostics
	{
		bool active = false;
		std::uint64_t nativeConsumed = 0, rawConsumed = 0;
		std::uint64_t nativeAccepted = 0, rawAccepted = 0;
		Pointer::Sample nativeLatest{}, rawLatest{};
		bool rawBound = false, rawDisabled = false;
		DWORD rawActivityTime = 0;
	} barPointerDragDiagnostics;

	void RecordBarDragPointerArrival(bool raw, const Pointer::Sample& sample)
	{
		auto& trace = barPointerDragDiagnostics;
		if (!trace.active) return;
		if (raw) { ++trace.rawConsumed; trace.rawLatest = sample; }
		else { ++trace.nativeConsumed; trace.nativeLatest = sample; }
	}

	void RecordBarDragPointerDecision(bool raw, bool accepted)
	{
		auto& trace = barPointerDragDiagnostics;
		if (!trace.active) return;
		if (accepted) ++(raw ? trace.rawAccepted : trace.nativeAccepted);
		trace.rawBound = barMouseGesture.RawBound();
		trace.rawDisabled = barMouseGesture.RawDisabled();
		trace.rawActivityTime = barMouseGesture.RawActivityTime();
	}

	void PublishBarPointerCancellation()
	{
		barPointerCancelSerial.fetch_add(1, std::memory_order_release);
		barPointerMouse.Cancel();
	}

	void RequestBarPointerCancellation()
	{
		const auto ordinal = barPointerActiveOrdinal.load(std::memory_order_acquire);
		if (ordinal) barPointerRequestedCancel.store(ordinal, std::memory_order_release);
	}

	bool EnqueueBarPointerSample(HWND window, ExMessage message,
		const Pointer::Sample& sample)
	{
		if (sample.kind == Pointer::Kind::Down && sample.source == Pointer::Source::Touch)
			BarAutoCollapseNativeDownSerial.fetch_add(1, std::memory_order_acq_rel);
		std::lock_guard lock(barPointerMetadataMutex);
		const unsigned short token = barPointerNextToken++;
		if (barPointerNextToken >= 0x4000u) barPointerNextToken = 16;
		auto& metadata = barPointerMetadata[token % barPointerMetadata.size()];
		metadata = { token, sample,
			barPointerCancelSerial.load(std::memory_order_acquire) };
		// 仅固化入队时的来源准入；实际会话、业务与 Raw 认领仍只在交互线程推进。
		if (sample.source == Pointer::Source::Mouse && sample.kind == Pointer::Kind::Down)
			(void)metadata.mouseAdmission.Begin(sample);
		// 非滚轮 wheel 仅携带 Bar 私有索引，HiMsg 不解释它，也不承载 Raw Mouse。
		message.wheel = static_cast<short>(token
			| (sample.source == Pointer::Source::Touch ? 0x8000u : 0u)
			| (sample.kind == Pointer::Kind::Cancel ? 0x4000u : 0u));
		message.category = EM_MOUSE;
		if (Inkeys::Window::Enqueue(window, message)) return true;
		barPointerCancelSerial.fetch_add(1, std::memory_order_release);
		return false;
	}

	bool DecodeBarPointerSample(ExMessage& message, BarQueuedPointerSample& queued)
	{
		const auto marker = static_cast<unsigned short>(message.wheel);
		const unsigned short token = marker & 0x3fffu;
		if (message.message == WM_MOUSEWHEEL || message.message == WM_MOUSEHWHEEL
			|| token < 16) return false;
		std::lock_guard lock(barPointerMetadataMutex);
		auto& stored = barPointerMetadata[token % barPointerMetadata.size()];
		if (stored.token != token)
		{
			queued.sample.kind = Pointer::Kind::Cancel;
			queued.sample.screen = barPointerLatest.screen;
			queued.cancelSerial = barPointerObservedCancel;
			message = {};
			message.message = WM_CANCELMODE;
			message.wheel = BarPointerCancelMessageMarker;
			return true;
		}
		queued = stored;
		stored.token = 0;
		if (queued.sample.source == Pointer::Source::Touch)
			MarkBarTouchPointerMessage(message,
				queued.sample.kind == Pointer::Kind::Cancel, true);
		else message.wheel = 0;
		return true;
	}

	void FinishBarPointerSession()
	{
		RecordBarDragPointerDecision(false, false);
		if (barMouseArmed) barPointerMouse.FinishMouse();
		barMouseArmed = false;
		barPointerActiveOrdinal.store(0, std::memory_order_release);
		barPointerSession.Reset();
		barMouseGesture.Reset();
	}

	bool BarPointerSessionActive()
	{
		return barPointerSession.Active() || barMouseGesture.Active();
	}

	void PrepareBarMouseDragTracking()
	{
		if (!barMouseGesture.Active() || barMouseArmed) return;
		// 只有连续交互候选准备私有补充；注册或配对失败不影响 HiMsg 点击。
		const auto& down = barMouseGesture.Latest();
		barMouseArmed = barPointerMouse.BeginMouse(down.screen, down.messageTime);
	}

	void BeginBarMouseDrag()
	{
		(void)barMouseGesture.BeginDrag();
	}

	void MakeBarPointerCancel(ExMessage& message)
	{
		FinishBarPointerSession();
		barPointerLatest.kind = Pointer::Kind::Cancel;
		message = {};
		message.category = EM_MOUSE;
		message.message = WM_CANCELMODE;
		message.wheel = BarPointerCancelMessageMarker;
		message.x = SHRT_MIN;
		message.y = SHRT_MIN;
	}

	ULONGLONG BarPointerSampleTick()
	{
		return barPointerSampleReady ? barPointerLatest.tick : GetTickCount64();
	}

	POINT BarPointerScreenPoint(const ExMessage& message)
	{
		if (barPointerSampleReady) return barPointerLatest.screen;
		POINT point{ message.x, message.y };
		if (!IsBarTouchScreenMessage(message)) BarLayoutToScreen(point);
		return point;
	}
}

void PrepareBarInteractionMessage(ExMessage& message,
	bool preserveTouchScreenCoordinates,
	BarTouchScreenSample* touchScreenSample = nullptr)
{
	if (touchScreenSample) touchScreenSample->ready = false;
	if (IsBarTouchScreenMessage(message))
	{
		const POINT screenPoint = barPointerSampleReady
			? barPointerLatest.screen : POINT{ message.x, message.y };
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

namespace
{
	Inkeys::UI::Bar::Ui3FixtureWireValue FixtureWireFromMessage(const ExMessage& message) noexcept
	{
		return { reinterpret_cast<std::uintptr_t>(message.hwnd), message.message,
			(message.lbutton ? 1u : 0u) | (message.mbutton ? 2u : 0u) | (message.rbutton ? 4u : 0u),
			(message.ctrl ? 1u : 0u) | (message.shift ? 2u : 0u), message.category,
			message.x, message.y, message.wheel };
	}
	bool ReadBarPointerPosition(POINT* point) noexcept
	{
		if (!point) return false;
		bool leftDown = false;
		const auto result = Inkeys::UI::Bar::ReadFixturePointerForCurrentOwner(*point, leftDown);
		if (result == Inkeys::UI::Bar::Ui3FixturePointResult::NotInstalled) return GetCursorPos(point) != FALSE;
		return result == Inkeys::UI::Bar::Ui3FixturePointResult::Available;
	}
}

namespace Inkeys::UI::Bar
{
	Message::Reply DispatchAuthorizedFixtureIndex(HWND window, UINT message, WPARAM index, LPARAM extra) noexcept
	{
		if (message == Ui3FixtureIndexMessage)
		{
			Ui3FixtureWireValue wire;
			if (ReceiveAuthorizedFixtureIndex(window, index, extra, wire))
			{
				ExMessage input{};
				input.message = static_cast<USHORT>(wire.message);
				input.hwnd = window; input.category = EM_MOUSE;
				input.x = wire.x; input.y = wire.y; input.lbutton = wire.buttons == 1;
				MarkBarTouchPointerMessage(input, wire.wheel == BarTouchCancelScreenMessageMarker, true);
				bool enqueued = false;
				try
				{
					Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, input.lbutton);
					enqueued = Inkeys::Window::Enqueue(window, input);
				}
				catch (...) { RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::Enqueue); }
				FinishAuthorizedFixtureEnqueue(static_cast<std::uint32_t>(index), enqueued);
				if (!enqueued)
				{
					try { Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false); }
					catch (...) { RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::Enqueue); }
				}
			}
			// Discard 仍会调用原 WndProc，私有输入与被拒 OS 输入必须 Handled。
			return { Message::Action::Handled, 0 };
		}
		LRESULT result = 0;
		if (AuthorizedFixtureWindowMessageMustBeHandled(window, message, index, extra, result))
			return { Message::Action::Handled, result };
		return {};
	}
}

namespace
{
	using AcceptedStrokeBegin = Inkeys::Drawing::Draw3::Bridge::AcceptedStrokeBegin;
	std::mutex barAcceptedStrokeMutex;
	std::array<AcceptedStrokeBegin, 32> barAcceptedStrokes{};
	std::size_t barAcceptedStrokeRead = 0, barAcceptedStrokeCount = 0;
	Inkeys::UI::Bar::OutsideCollapsePending barOutsideCollapse;
	struct BarAutoCollapseContext
	{
		std::uint64_t modeRevision = 0, sceneRevision = 0, inputSerial = 0;
		int outsidePolicy = 0;
		Inkeys::UI::Bar::BarBottomDockMode dock{};
		bool blocked = true, folded = true;
		bool operator==(const BarAutoCollapseContext&) const = default;
	} barAutoCollapseContext;
	std::uint64_t barAutoCollapseContextRevision = 1;

	bool FoldBarAutomatically(bool preserveCurrentDrag = false)
	{
		if (barUISet.barState.fold) return false;
		barUISet.barState.fold = true;
		if (preserveCurrentDrag)
			barPointerFoldAtDown = true; // 仅本手势发起的收起可以更新其基线。
		barUISet.CollapseAuxiliaryPanels(!preserveCurrentDrag);
		barUISet.ClearBottomDockCenterForFold();
		Inkeys::UI::Bar::ClearWhiteboardDockLock();
		barUISet.UpdateRendering(false);
		return true;
	}

}

bool BarUISetClass::IsAutoCollapsePointerHit(POINT point)
{
	const HWND hit = WindowFromPoint(point);
	if (hit && (hit == floating_window || IsChild(floating_window, hit))) return true;
	// 使用成功上屏的区域，不能把整个透明 HWND 包络当成可点击主栏。
	std::lock_guard lock(barUISet.borderCursorLightMutex);
	for (std::size_t i = 0; i < barUISet.borderCursorVisibleRegionCount; ++i)
		if (PtInRect(&barUISet.borderCursorVisibleRegions[i], point)) return true;
	return false;
}

void BarUISetClass::PollGlobalMouseInput()
{
	using namespace Inkeys::UI::Bar;
	namespace Hook = Inkeys::Input::MouseHook;
	const auto mode = GetStateModeVersionedSnapshot();
	const auto dock = barUISet.bottomDockMode.load(std::memory_order_acquire);
	const bool blocked = offSignal || !floating_window || !IsWindowVisible(floating_window)
		|| Inkeys::UI::Setting::IsVisible() || Inkeys::UI::MessageBox::IsShowing();
	const int outsidePolicy = Inkeys::config.UI.Bar.AutoCollapse.OutsideClick;
	const BarAutoCollapseContext context{ mode.revision, AutoCollapseSceneRevision(),
		BarAutoCollapseNativeDownSerial.load(std::memory_order_acquire),
		outsidePolicy, dock, blocked, static_cast<bool>(barUISet.barState.fold) };
	if (!(context == barAutoCollapseContext))
	{
		barAutoCollapseContext = context;
		++barAutoCollapseContextRevision;
		barOutsideCollapse.Cancel();
	}
	const bool outsideEnabled = !blocked && !context.folded
		&& IsComputerInteractionMode(mode.state.StateModeSelect)
		&& ShouldAutoCollapse(outsidePolicy, dock);
	Hook::PublishRoute({ !blocked && !IsComputerInteractionMode(mode.state.StateModeSelect),
		outsideEnabled, barAutoCollapseContextRevision });
	Hook::Event event;
	// 顶层及颜色/粗细/Seek 嵌套等待共用此入口，有限批量不会饿死原生输入。
	for (unsigned int i = 0; i < 16 && Hook::TryPop(event); ++i)
	{
		if (event.kind == Hook::Kind::Wheel) Hook::DispatchWheel(event);
		else if (outsideEnabled && event.contextRevision == barAutoCollapseContextRevision)
		{
			if (IsAutoCollapsePointerHit(POINT{ event.screen.x, event.screen.y })) barOutsideCollapse.Cancel();
			else barOutsideCollapse.Arm(
				Inkeys::Window::MessageDispatch::PostedTick(event.time),
				barAutoCollapseContextRevision);
		}
	}
	Hook::PollPptWheel();
	if (BarAutoCollapseNativeDownSerial.load(std::memory_order_acquire) != context.inputSerial)
		barOutsideCollapse.Cancel();
	if (barOutsideCollapse.Take(GetTickCount64(), barAutoCollapseContextRevision, outsideEnabled))
		FoldBarAutomatically();

	std::array<AcceptedStrokeBegin, 16> notices{};
	std::size_t count = 0;
	{
		std::lock_guard lock(barAcceptedStrokeMutex);
		while (count < notices.size() && barAcceptedStrokeCount != 0)
		{
			notices[count++] = barAcceptedStrokes[barAcceptedStrokeRead];
			barAcceptedStrokeRead = (barAcceptedStrokeRead + 1) % barAcceptedStrokes.size();
			--barAcceptedStrokeCount;
		}
	}
	for (std::size_t i = 0; i < count; ++i)
	{
		const auto& notice = notices[i];
		if (offSignal || !Inkeys::Drawing::Draw3::ProductHost().IsAcceptedStrokeCurrent(notice)) continue;
		const int policy = notice.kind == Inkeys::Drawing::Draw3::Bridge::CompletedStrokeKind::Eraser
			? Inkeys::config.UI.Bar.AutoCollapse.Erasing : Inkeys::config.UI.Bar.AutoCollapse.Drawing;
		// 已完成的短笔迹仍是有效起笔；只检查场景身份，不检查聚合活动计数。
		if (ShouldAutoCollapse(policy, barUISet.bottomDockMode.load(std::memory_order_acquire)))
			FoldBarAutomatically();
	}
}

bool TryReadBarPointerMessage(ExMessage& message, BYTE filter, HWND window,
	bool preserveScreenCoordinates, BarTouchScreenSample* screenSample)
{
	if (screenSample) screenSample->ready = false;
	barUISet.PollGlobalMouseInput();
	if (barKeyboardShortcuts.Poll(false))
	{
		// 不在嵌套手势中执行命令，先走已有 cancel 路径退出控件引用栈。
		MakeBarPointerCancel(message);
		return true;
	}
	for (;;)
	{
		const auto ordinal = barPointerActiveOrdinal.load(std::memory_order_acquire);
		if (ordinal && barPointerRequestedCancel.load(std::memory_order_acquire) == ordinal)
		{
			MakeBarPointerCancel(message);
			return true;
		}
		if (BarPointerSessionActive()
			&& (barMouseGesture.SourceReplaced()
				|| !Pointer::CheckInputDesktop()
				|| GetStateModeVersionedSnapshot().revision != barPointerModeRevision
				|| static_cast<bool>(barUISet.barState.fold) != barPointerFoldAtDown))
		{
			// 系统桌面失效在 owner 上撤销触摸租约；工具/折叠切换只取消当前交互。
			if (!Pointer::CheckInputDesktop())
				PostMessageW(window, WM_CANCELMODE, 0, 0);
			MakeBarPointerCancel(message);
			return true;
		}
		const auto cancelSerial = barPointerCancelSerial.load(std::memory_order_acquire);
		if (barPointerObservedCancel != cancelSerial)
		{
			barPointerObservedCancel = cancelSerial;
			if (BarPointerSessionActive())
			{
				MakeBarPointerCancel(message);
				return true;
			}
		}
		Pointer::Sample sample;
		if ((filter & EM_MOUSE) && barMouseArmed && barPointerMouse.TryPop(sample))
		{
			RecordBarDragPointerArrival(true, sample);
			const bool cancelled = sample.kind == Pointer::Kind::Cancel;
			const bool accepted = barMouseGesture.AcceptRaw(sample);
			RecordBarDragPointerDecision(true, accepted);
			if (cancelled)
			{
				barPointerMouse.FinishMouse();
				barMouseArmed = false;
				// 共享策略区分未绑定补充与拖动传输失效；不以业务已拖动扩大取消。
				if (accepted)
				{
					MakeBarPointerCancel(message);
					return true;
				}
			}
			if (!accepted) continue;
			sample = barMouseGesture.Latest();
			barPointerLatest = sample;
			barPointerSampleReady = true;
			message = {};
			message.category = EM_MOUSE;
			message.message = sample.kind == Pointer::Kind::Up ? WM_LBUTTONUP : WM_MOUSEMOVE;
			message.lbutton = sample.kind != Pointer::Kind::Up;
			POINT point = sample.screen;
			BarScreenToLayout(point);
			message.x = static_cast<short>(clamp<LONG>(point.x, SHRT_MIN, SHRT_MAX));
			message.y = static_cast<short>(clamp<LONG>(point.y, SHRT_MIN, SHRT_MAX));
			ApplyBarBottomDockRigidHitTest(message);
			if (screenSample) *screenSample = { sample.screen, true };
			if (sample.kind == Pointer::Kind::Up)
			{
				barMouseArmed = false;
				barPointerActiveOrdinal.store(0, std::memory_order_release);
				barPointerMouse.FinishMouse();
			}
			return true;
		}
		BarQueuedPointerSample queued;
		bool hasMetadata = false;
		if (barPointerPending)
		{
			message = barPointerPendingMessage;
			queued = *barPointerPending;
			barPointerPending.reset();
			hasMetadata = true;
		}
		else
		{
			if (!Inkeys::Window::TryGet(window, message,
				static_cast<Inkeys::Message::Filter>(filter))) return false;
			if (!Inkeys::UI::Bar::ObserveAuthorizedFixtureDequeue(FixtureWireFromMessage(message))) return false;
			hasMetadata = (filter & EM_MOUSE) && IsBarCoordinateMessage(message.message)
				&& DecodeBarPointerSample(message, queued);
		}
		if (hasMetadata) RecordBarDragPointerArrival(false, queued.sample);
		if (hasMetadata && queued.cancelSerial < barPointerObservedCancel) continue;
		if (IsBarPointerCancelMessage(message))
		{
			if (hasMetadata && queued.sample.kind == Pointer::Kind::Cancel
				&& queued.sample.source == Pointer::Source::Touch
				&& !barPointerSession.Accept(queued.sample)) continue;
			MakeBarPointerCancel(message);
			return true;
		}
		const bool touch = IsBarTouchPointerMessage(message);
		const bool down = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK;
		const bool pointer = down || message.message == WM_MOUSEMOVE || message.message == WM_LBUTTONUP;
		if (!pointer)
		{
			if (BarPointerSessionActive() && IsBarCoordinateMessage(message.message)) continue;
			PrepareBarInteractionMessage(message, preserveScreenCoordinates, screenSample);
			return true;
		}
		if (!hasMetadata)
		{
			queued.sample.source = touch ? Pointer::Source::Touch : Pointer::Source::Mouse;
			queued.sample.kind = down ? Pointer::Kind::Down
				: message.message == WM_LBUTTONUP ? Pointer::Kind::Up : Pointer::Kind::Move;
			queued.sample.screen = POINT{ message.x, message.y };
			if (!IsBarTouchScreenMessage(message)) BarLayoutToScreen(queued.sample.screen);
			queued.sample.tick = GetTickCount64();
			queued.sample.generation = touch
				? (down ? ++barFixtureTouchGeneration
					: barPointerSession.Latest().generation) : 0;
			queued.cancelSerial = barPointerObservedCancel;
		}
		sample = queued.sample;
		if (!hasMetadata) RecordBarDragPointerArrival(false, sample);
		if (down)
		{
			if (BarPointerSessionActive())
			{
				if (!touch && barPointerSession.Active()) continue;
				barPointerPending = queued;
				barPointerPendingMessage = message;
				MakeBarPointerCancel(message);
				return true;
			}
			if (touch) (void)barPointerSession.Begin(sample);
			else if (hasMetadata)
			{
				barMouseGesture = queued.mouseAdmission;
				if (!barMouseGesture.Active() || barMouseGesture.SourceReplaced())
				{
					barMouseGesture.Reset();
					continue;
				}
			}
			else if (!barMouseGesture.Begin(sample)) continue;
			barPointerActiveOrdinal.store(++barPointerNextOrdinal, std::memory_order_release);
			barPointerModeRevision = GetStateModeVersionedSnapshot().revision;
			barPointerFoldAtDown = barUISet.barState.fold;
		}
		else if (touch)
		{
			const bool accepted = barPointerSession.Accept(sample);
			RecordBarDragPointerDecision(false, accepted);
			if (!accepted) continue;
		}
		else if (barMouseGesture.Active())
		{
			const bool accepted = barMouseGesture.AcceptNative(sample);
			RecordBarDragPointerDecision(false, accepted);
			if (!accepted) continue;
			sample = barMouseGesture.Latest();
			if (sample.screen.x != queued.sample.screen.x || sample.screen.y != queued.sample.screen.y)
			{
				POINT layoutPoint = sample.screen;
				BarScreenToLayout(layoutPoint);
				message.x = static_cast<short>(clamp<LONG>(layoutPoint.x, SHRT_MIN, SHRT_MAX));
				message.y = static_cast<short>(clamp<LONG>(layoutPoint.y, SHRT_MIN, SHRT_MAX));
			}
			if (sample.kind == Pointer::Kind::Cancel)
			{
				MakeBarPointerCancel(message);
				return true;
			}
		}
		else if (barPointerSession.Active() || sample.kind == Pointer::Kind::Up
			|| message.lbutton) continue;
		if (sample.kind == Pointer::Kind::Up)
		{
			if (barMouseArmed) barPointerMouse.FinishMouse();
			barMouseArmed = false;
			barPointerActiveOrdinal.store(0, std::memory_order_release);
		}
		barPointerLatest = sample;
		barPointerSampleReady = true;
		PrepareBarInteractionMessage(message, preserveScreenCoordinates, screenSample);
		return true;
	}
}

bool WaitForBarInteractionMessage(ExMessage& message, BYTE filter, HWND hWnd,
	bool preserveTouchScreenCoordinates = false,
	BarTouchScreenSample* touchScreenSample = nullptr)
{
	while (!offSignal)
	{
		if (TryReadBarPointerMessage(message, filter, hWnd,
			preserveTouchScreenCoordinates, touchScreenSample)) return true;
		this_thread::sleep_for(chrono::milliseconds(1));
	}
	return false;
}

bool TryGetBarInteractionMessage(ExMessage* message, BYTE filter,
	bool removeMessage, HWND hWnd, BarTouchScreenSample* touchScreenSample = nullptr)
{
	return message && removeMessage && TryReadBarPointerMessage(
		*message, filter, hWnd, false, touchScreenSample);
}

void ClearBarInteractionMessages(BYTE filter, HWND)
{
	Inkeys::UI::Bar::ObserveAuthorizedFixtureClear(filter, 0);
	// 只退休本次会话；不能清掉 HiMsg 中下一次 Down 或另一来源的输入。
	FinishBarPointerSession();
}

void QueueBarThicknessSliderEnd(HWND)
{
	RequestBarPointerCancellation();
}

void QueueBarColorPickerEnd(HWND)
{
	RequestBarPointerCancellation();
}

// ====================
// 窗口

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_CREATE)
	{
		(void)Pointer::AttachRawMouse(hWnd);
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
	if (msg == WM_DESTROY)
	{
		barUISet.ShutdownWindowInput(hWnd);
		Pointer::DetachRawMouse(hWnd);
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
	if (msg == WM_CANCELMODE)
	{
		// Whiteboard Exit 会通过 Window Service 广播取消消息，清掉 Bar 的捕获和浮层输入。
		barUISet.ShutdownWindowInput(hWnd);
		barUISet.UpdateRendering(false);
		return 0;
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
		BarAutoCollapseNativeDownSerial.fetch_add(1, std::memory_order_acq_rel);

	switch (msg)
	{
	case WM_MOUSEACTIVATE:
		// Bar 从创建起不激活；返回该值仍会继续投递本次鼠标点击。
		return MA_NOACTIVATE;

	case WM_INPUT:
	{
		if (Pointer::HandleRawMouse(lParam))
		{
			Inkeys::Drawing::Draw3::RecordCursorRawInput(lParam);
			barUISet.RegisterBorderCursorLight(hWnd);
		}
		// 私有有序通道供交互线程消费，默认过程仍负责 HRAWINPUT 清理。
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	case WM_TIMER:
	{
		if (wParam == BarBorderCursorGraceTimerId)
		{
			barUISet.HandleBorderCursorGraceTimeout(hWnd);
			return 0;
		}
		if (wParam == BarRecallChromeIdleTimerId)
		{
			// 浮窗静止时渲染线程在休眠。到期只唤醒一帧；提前触发则按剩余时间重装。
			unsigned retryMs = 0;
			if (barUISet.PollRecallChromeIdleTimer(retryMs))
				barUISet.UpdateRendering(false);
			else if (retryMs != 0)
				SetTimer(hWnd, BarRecallChromeIdleTimerId, retryMs, nullptr);
			else
				KillTimer(hWnd, BarRecallChromeIdleTimerId);
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
			bool pointAvailable = ReadBarPointerPosition(&point)
				&& BarScreenToLayout(point);
			if (pointAvailable)
				point.y = barUISet.BottomDockRigidHitTestY(point.y);
			auto infoHit = barUISet.shapeMap[annotation
				? BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit
				: BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit];
			auto popup = barUISet.shapeMap[annotation
				? BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup
				: BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup];
			const auto stateMode = GetStateModeSnapshot();
				bool available = barUISet.barState.drawAttribute
					&& !barUISet.barState.fold
					&& (annotation
						? static_cast<bool>(drawAttribute.penTypeMenuOpen)
							&& !stateMode.laserActive
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
						barUISet.barStyle.dpiZoom, stateMode).supported;
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

	case BarBorderCursorSurfacePointerMessage:
	{
		if (wParam != 0)
			barUISet.ActivateBorderCursorTracking(hWnd);
		else
		{
			// PageControl 的真实离开与 Bar 自身 WM_MOUSELEAVE 使用同一解锁语义。
			{
				lock_guard lock(barUISet.borderCursorLightMutex);
				barUISet.borderCursorActivationBlockedUntilLeave = false;
			}
			barUISet.RegisterBorderCursorLight(hWnd);
		}
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
		// Raw Mouse / WM_TOUCH 拥有手势终态；捕获只是兼容设施，丢失不能伪造 Up。
		barUISet.barState.drawAttributeBar.colorPickerPointerCapture = false;
		barUISet.barState.drawAttributeBar.thicknessSliderCapture = false;
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	case WM_MOUSELEAVE:
	{
		if (barUISet.eraserAttribute.ResetPointerFeedback()) barUISet.UpdateRendering(false);
		if (barUISet.recallAttribute.ResetPointerFeedback()) barUISet.UpdateRendering(false);
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
		const UINT count = LOWORD(wParam);
		vector<TOUCHINPUT> inputs(count);
		if (GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), count,
			inputs.data(), sizeof(TOUCHINPUT)))
		{
			bool hasPrimary = false;
			for (const auto& input : inputs)
				hasPrimary = hasPrimary || (input.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
			bool fallbackLocked = false;
			auto PublishTouch = [&](Pointer::Kind kind, POINT point)
				{
					Pointer::Sample sample;
					sample.kind = kind;
					sample.source = Pointer::Source::Touch;
					sample.screen = point;
					sample.messageTime = static_cast<DWORD>(GetMessageTime());
					sample.tick = Inkeys::Window::MessageDispatch::PostedTick(sample.messageTime);
					sample.generation = barTouchProducer.generation;
					sample.device = reinterpret_cast<std::uintptr_t>(barTouchProducer.source);
					sample.contact = barTouchProducer.id;
					ExMessage translated{};
					translated.message = kind == Pointer::Kind::Down ? WM_LBUTTONDOWN
						: kind == Pointer::Kind::Move ? WM_MOUSEMOVE : WM_LBUTTONUP;
					translated.x = static_cast<short>(clamp<LONG>(point.x, SHRT_MIN, SHRT_MAX));
					translated.y = static_cast<short>(clamp<LONG>(point.y, SHRT_MIN, SHRT_MAX));
					translated.lbutton = kind == Pointer::Kind::Down || kind == Pointer::Kind::Move;
					if (!EnqueueBarPointerSample(hWnd, translated, sample))
					{
						barPointerMouse.EndTouch(barTouchProducer.generation);
						barTouchProducer = {};
					}
				};
			// 新 primary 先取消旧 fallback，再处理旧 Up，避免旧触点产生点击。
			for (int pass = 0; pass < 2; ++pass)
			for (const auto& input : inputs)
			{
				const bool primary = (input.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
				if (primary != (pass == 0)) continue;
				const POINT point{ static_cast<LONG>(std::lround(input.x / 100.0)),
					static_cast<LONG>(std::lround(input.y / 100.0)) };
				const bool sameContact = barTouchProducer.active
					&& barTouchProducer.source == input.hSource
					&& barTouchProducer.id == input.dwID;
				const bool fallback = !hasPrimary && !barTouchProducer.active && !fallbackLocked;
				if ((input.dwFlags & TOUCHEVENTF_DOWN) && (primary || fallback))
				{
					if (barTouchProducer.active && !sameContact)
					{
						PublishTouch(Pointer::Kind::Cancel, barTouchProducer.point);
						barPointerMouse.EndTouch(barTouchProducer.generation);
						barTouchProducer = {};
					}
					if (!barTouchProducer.active)
					{
						const auto generation = ++barTouchGeneration;
						if (!barPointerMouse.BeginTouch(generation)) continue;
						barTouchProducer = { input.hSource, input.dwID, true,
							primary, point, generation };
						fallbackLocked = !primary;
						Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, true);
						PublishTouch(Pointer::Kind::Down, point);
					}
				}
				if (!barTouchProducer.active || barTouchProducer.source != input.hSource
					|| barTouchProducer.id != input.dwID) continue;
				if (primary) barTouchProducer.primary = true;
				barTouchProducer.point = point;
				if (input.dwFlags & TOUCHEVENTF_MOVE) PublishTouch(Pointer::Kind::Move, point);
				if (input.dwFlags & TOUCHEVENTF_UP)
				{
					PublishTouch(Pointer::Kind::Up, point);
					barPointerMouse.EndTouch(barTouchProducer.generation);
					barTouchProducer = {};
					Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);
				}
			}
		}
		else
		{
			PublishBarPointerCancellation();
			barPointerMouse.EndTouch(barTouchProducer.generation);
			barTouchProducer = {};
		}
		CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
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

void BarUISetClass::CollapseAuxiliaryPanels(bool cancelCapture)
{
	eraserAttribute.Close(*this);
	recallAttribute.Close(*this);
	barState.drawAttribute = false;
	barState.geometryAttribute = false;
	barState.moreExpanded = false;
	ClosePenTypeMenu();
	CloseDrawAttributeTooltips();
	CloseThicknessSlider(cancelCapture);
	CloseColorPicker(cancelCapture);
}

void BarUISetClass::ArmRecallChromeIdleTimer() noexcept
{
	if (!floating_window || !IsWindow(floating_window)) return;
	SetTimer(floating_window, BarRecallChromeIdleTimerId,
		static_cast<UINT>(Inkeys::UI::Bar::RecallChromeIdleSeconds * 1000.0), nullptr);
}

void BarUISetClass::DisarmRecallChromeIdleTimer() noexcept
{
	if (!floating_window || !IsWindow(floating_window)) return;
	KillTimer(floating_window, BarRecallChromeIdleTimerId);
}

void BarUISetClass::ShutdownWindowInput(HWND hWnd)
{
	PublishBarPointerCancellation();
	barPointerMouse.EndTouch(barTouchProducer.generation);
	barTouchProducer = {};
	eraserAttribute.ResetPointerFeedback();
	recallAttribute.ResetPointerFeedback();
	if (!hWnd) return;

	// 先撤销输入源，避免 ReleaseCapture 重入时继续向交互队列投递手势。
	KillTimer(hWnd, BarBorderCursorGraceTimerId);
	KillTimer(hWnd, BarThicknessAnnotationTooltipGraceTimerId);
	KillTimer(hWnd, BarThicknessOverflowTooltipGraceTimerId);
	KillTimer(hWnd, BarRecallChromeIdleTimerId);
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
		DrawAttributeSoftPen,
		DrawAttributeLaser,
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
		drawAttributeSoftPenHoverStage(owner.drawAttributeSoftPenHoverStage),
		drawAttributeLaserHoverStage(owner.drawAttributeLaserHoverStage),
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

	static int RunThicknessFineDialProductionTest() noexcept
	{
		const char* stage = "fine-dial-startup-isolation";
		try
		{
			using namespace Inkeys::Drawing::Draw3;
			if (ProductRunning() || Inkeys::Window::GetService().Running()) return 2;
			std::atomic<unsigned long long> clickPulse = 0;
			BarInteractionSession session(::barUISet, clickPulse, {});
			auto& shared = session.barState.drawAttributeBar;
			session.barStyle.dpiZoom = 1.0;
			session.barState.fold = false;
			session.barState.drawAttribute = true;
			shared.thicknessViewMode = ThicknessViewMode::FineDial;
			const auto require = [](bool passed, const char* check)
			{
				if (!passed) throw check;
			};
			const auto bridgeRevision = [&]
			{
				return ProductHost().ProductBridge().Snapshot().revision;
			};
			ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
			require(SetPenWidth(7.0f, false), "fine-dial-seed-width");
			const auto range = GetBarThicknessSliderRange(PenModeSelectEnum::IdtPenSoftPen, 1.0);
			require(range.supported && range.min <= 7 && range.max >= 14, "fine-dial-real-range");
			const auto begin = [&](double candidate)
			{
				session.BeginThicknessFineDialDrag(candidate, 100.0, 4.0, range,
					StateModeTransitionRevision());
				require(shared.thicknessFineDialCandidateActive &&
					shared.thicknessSliderCandidateWidth == candidate, "fine-dial-candidate-published");
			};
			const auto cleared = [&]
			{
				return session.thicknessFineDialModeRevision == 0 &&
					session.thicknessFineDialPhase == ThicknessFineDialPhase::Idle &&
					!shared.thicknessFineDialCandidateActive &&
					!shared.thicknessFineDialDragging && !shared.thicknessFineDialPhysicsActive &&
					shared.thicknessSliderCandidateWidth == 0.0f;
			};

			stage = "width-dip-ui-contract";
			using namespace Inkeys::Business::PenWidth;
			auto widthSnapshot = GetStateModeSnapshot();
			widthSnapshot.penWidthContext = { 42, 0, 264.0, 2560, 1440 };
			widthSnapshot.Pen.Brush1.widthSelection = { Source::Preset, Preset::Medium, 3.0 };
			const auto metrics = GetPenWidthMetrics(widthSnapshot);
			require(abs(metrics.idealCanvasWidthPx - 5.333333333333333) < 0.000001,
				"bar-production-continuous-1440p");
			require(abs(GetBarCurrentPenThicknessVisualWidth(1.0, widthSnapshot)
				- GetBarCurrentPenThicknessVisualWidth(3.0, widthSnapshot)) < 0.000001,
				"bar-custom-layout-zoom-does-not-affect-ink");
			require(IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen, 1, 3.0,
				widthSnapshot) && !IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen,
				0, 3.0, widthSnapshot), "bar-preset-identity");
			auto overflowSnapshot = widthSnapshot;
			overflowSnapshot.penWidthContext = { 43, 1, 192.0, 1920, 1080 };
			overflowSnapshot.Pen.Brush1.widthSelection = { Source::ExplicitDip, Preset::Medium, 30.0 };
			require(GetBarCurrentPenThicknessVisualWidth(1.0, overflowSnapshot) == 60.0
				&& GetBarCurrentPenThicknessVisualWidth(3.0, overflowSnapshot) > 50.0,
				"bar-preview-overflow-compares-canvas-pixels");
			widthSnapshot.penWidthContext.pixelWidth = 320;
			widthSnapshot.penWidthContext.pixelHeight = 180;
			require(RoundPositiveCanvasPx(GetBarThicknessPresetPx(PenModeSelectEnum::IdtPenSoftPen,
				0, 1.0, widthSnapshot)) == RoundPositiveCanvasPx(GetBarThicknessPresetPx(
				PenModeSelectEnum::IdtPenSoftPen, 1, 1.0, widthSnapshot)), "bar-identity-collision-fixture");
			require(IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen, 1, 1.0,
				widthSnapshot) && !IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen,
				0, 1.0, widthSnapshot), "bar-identity-survives-pixel-collision");
			widthSnapshot.Pen.Brush1.widthSelection = { Source::ExplicitDip, Preset::Medium, 0.2 };
			const auto expandedRange = GetBarThicknessSliderRange(PenModeSelectEnum::IdtPenSoftPen,
				3.0, widthSnapshot);
			require(expandedRange.min <= 0.2 && expandedRange.max >= 30.0,
				"bar-slider-preserves-valid-sub-dip");
			widthSnapshot.Pen.Highlighter1.widthSelection = { Source::ExplicitDip, Preset::Medium, 200.0 };
			widthSnapshot.Pen.ModeSelect = PenModeSelectEnum::IdtPenHighlighter1;
			const auto highlighterRange = GetBarThicknessSliderRange(PenModeSelectEnum::IdtPenHighlighter1,
				1.0, widthSnapshot);
			require(highlighterRange.min <= 16.0 && highlighterRange.max >= 200.0,
				"bar-slider-new-thin-and-old-custom-available");
			require(SetPenWidthPresetIfRevision(1, StateModeTransitionRevision(), false),
				"bar-select-preset-through-production-setter");
			const auto beforeNoOp = bridgeRevision();
			const auto beforeNoOpSelection = GetPenWidthSelection(GetStateModeSnapshot());
			session.BeginThicknessFineDialDrag(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip,
				100.0, 4.0, range, StateModeTransitionRevision());
			session.CommitThicknessFineDialSelection();
			require(bridgeRevision() == beforeNoOp && GetPenWidthSelection(GetStateModeSnapshot()).source
				== beforeNoOpSelection.source, "fine-dial-open-commit-preserves-preset");
			require(SetPenWidth(1.939393939393939, false), "fine-dial-fractional-origin");
			const auto fractionalWidth = GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip;
			session.BeginThicknessFineDialDrag(fractionalWidth, 100.0, 4.0, range,
				StateModeTransitionRevision());
			require(abs(static_cast<double>(shared.thicknessSliderCandidateWidth) - fractionalWidth)
				< 0.000001, "fine-dial-entry-keeps-origin-fraction");
			session.thicknessFineDialRawValue = fractionalWidth + 0.5;
			require(abs(session.PublishThicknessFineDialCandidate() - (fractionalWidth + 1.0))
				< 0.000001, "fine-dial-positive-half-step");
			session.thicknessFineDialRawValue = fractionalWidth - 0.5;
			// 该真实量程下限为 1 DIP；负向半步不得越过端点。
			require(abs(session.PublishThicknessFineDialCandidate() - (std::max)(range.min, fractionalWidth - 1.0))
				< 0.000001, "fine-dial-negative-half-step-boundary-clamp");
			session.CancelThicknessFineDialSelection();
			const double interiorOrigin = fractionalWidth + 2.0;
			session.BeginThicknessFineDialDrag(interiorOrigin, 100.0, 4.0, range,
				StateModeTransitionRevision());
			session.thicknessFineDialRawValue = interiorOrigin - 0.5;
			require(abs(session.PublishThicknessFineDialCandidate() - (interiorOrigin - 1.0))
				< 0.000001, "fine-dial-negative-half-step-preserves-interior-fraction");
			session.thicknessFineDialRawValue = interiorOrigin + 0.5;
			require(abs(session.PublishThicknessFineDialCandidate() - (interiorOrigin + 1.0))
				< 0.000001, "fine-dial-interior-half-step-symmetry");
			session.CancelThicknessFineDialSelection();
			session.CommitThicknessFineDialSelection();
			require(abs(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip - fractionalWidth)
				< 0.000001, "fine-dial-cancel-preserves-fractional-origin");
			session.BeginThicknessFineDialDrag(fractionalWidth, 100.0, 4.0, range,
				StateModeTransitionRevision());
			session.thicknessFineDialRawValue = fractionalWidth + 1.0;
			session.PublishThicknessFineDialCandidate();
			const auto committedRevision = session.CommitThicknessFineDialSelection(false);
			require(committedRevision == StateModeTransitionRevision()
				&& GetPenWidthSelection(GetStateModeSnapshot()).source == Source::ExplicitDip
				&& abs(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip
					- (fractionalWidth + 1.0)) < 0.000001,
				"fine-dial-real-commit-keeps-fractional-origin");
			const double sliderAdjustedWidth = fractionalWidth + 2.25;
			session.BeginThicknessFineDialDrag(sliderAdjustedWidth, 100.0, 4.0, range,
				StateModeTransitionRevision(), true);
			session.CommitThicknessFineDialSelection(false);
			require(abs(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip - sliderAdjustedWidth)
				< 0.000001, "slider-to-fine-dial-retains-real-slider-adjustment");
			widthSnapshot.StateModeSelect = StateModeSelectEnum::IdtShape;
			widthSnapshot.Shape.ModeSelect = ShapeModeSelectEnum::IdtShapeRectangle1;
			widthSnapshot.Shape.Rectangle1.widthSelection = { Source::Preset, Preset::Thick, 3.0 };
			require(IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen, 2, 1.0,
				widthSnapshot) && !IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen,
				1, 1.0, widthSnapshot), "bar-shape-selected-uses-shape-slot");
			require(SetPenWidth(7.0f, false), "fine-dial-restore-existing-fixture");

			stage = "explicit-width-matches-preset-highlight";
			const auto originalContext = GetPenWidthContext();
			const int originalPaintDevice = setlist.paintDevice;
			const auto selectedOnly = [&](const StateModeClass& snapshot, int expected)
			{
				for (size_t index = 0; index < 3; ++index)
					require(IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen,
						index, 4.0, snapshot) == (static_cast<int>(index) == expected),
						"bar-only-matching-preset-highlighted");
			};
			const Context highlightContexts[] = {
				{ 50, 1, 96.0, 1920, 1080 }, { 51, 1, 264.0, 2560, 1440 },
				{ 52, 0, 96.0, 1920, 1080 }, { 53, 0, 264.0, 2560, 1440 },
				{ 54, 0, 264.0, 320, 180 }
			};
			for (const auto& context : highlightContexts)
			{
				setlist.paintDevice = context.paintDevice;
				ApplyPenWidthContext(context);
				for (const auto tool : { PenToolSelectionEnum::SoftPen, PenToolSelectionEnum::HardPen,
					PenToolSelectionEnum::Highlighter, PenToolSelectionEnum::Laser })
				{
					ChangeStateModeToPenTool(tool);
					for (size_t index = 0; index < 3; ++index)
					{
						const auto snapshot = GetStateModeSnapshot();
						const double targetDip = tool == PenToolSelectionEnum::Laser
							? GetBarLaserThicknessPresetDip(index, snapshot)
							: GetBarThicknessPresetMetrics(snapshot.Pen.ModeSelect, index, snapshot).resolvedWidthDip;
						require(SetPenWidth(targetDip + 1.0, false), "bar-seed-nonpreset-width");
						selectedOnly(GetStateModeSnapshot(), -1);
						if (tool == PenToolSelectionEnum::Laser)
							require(SetPenWidthIfRevision(targetDip, StateModeTransitionRevision(), false),
								"bar-laser-explicit-width-commit");
						else
						{
							const auto dialRange = GetBarThicknessSliderRange(snapshot.Pen.ModeSelect, 1.0, snapshot);
							session.BeginThicknessFineDialDrag(targetDip + 1.0, 100.0, 4.0,
								dialRange, StateModeTransitionRevision());
							session.thicknessFineDialRawValue = targetDip;
							session.PublishThicknessFineDialCandidate();
							require(session.CommitThicknessFineDialSelection(false) == StateModeTransitionRevision(),
								"bar-real-fine-dial-release-matches-preset");
						}
						const auto committed = GetStateModeVersionedSnapshot();
						const auto committedBridge = bridgeRevision();
						selectedOnly(committed.state, static_cast<int>(index));
						require(GetPenWidthSelection(committed.state).source == Source::ExplicitDip
							&& StateModeTransitionRevision() == committed.revision && bridgeRevision() == committedBridge,
							"bar-highlight-does-not-rewrite-explicit-source-or-state");
					}
				}

				stage = "shape-presets-use-shared-brush-policy";
				ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
				require(SetPenWidth(9.25, false), "bar-shape-independent-brush-seed");
				const auto brushSnapshot = GetStateModeSnapshot();
				ChangeStateModeToShape();
				for (const auto shape : { ShapeModeSelectEnum::IdtShapeStraightLine1,
					ShapeModeSelectEnum::IdtShapeDashedLine1, ShapeModeSelectEnum::IdtShapeRectangle1 })
				{
					SetShapeModeSelect(shape);
					const auto beforeShape = GetStateModeSnapshot();
					const auto otherSelection = shape == ShapeModeSelectEnum::IdtShapeRectangle1
						? beforeShape.Shape.StraightLine1.widthSelection : beforeShape.Shape.Rectangle1.widthSelection;
					for (size_t index = 0; index < 3; ++index)
					{
						const auto brushPreset = GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen,
							index, brushSnapshot);
						require(SetPenWidthPresetIfRevision(static_cast<int>(index), StateModeTransitionRevision(), false),
							"bar-shape-real-preset-button-setter");
						const auto presetSnapshot = GetStateModeSnapshot();
						const auto shapeMetrics = GetPenWidthMetrics(presetSnapshot);
						require(shapeMetrics.resolvedWidthDip == brushPreset.resolvedWidthDip
							&& shapeMetrics.appliedCanvasWidthPx == brushPreset.appliedCanvasWidthPx
							&& GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen,
								index, presetSnapshot).resolvedWidthDip == brushPreset.resolvedWidthDip,
							"bar-shape-click-readout-and-ink-use-brush-preset");
						selectedOnly(presetSnapshot, static_cast<int>(index));
						require(SetPenWidth(brushPreset.resolvedWidthDip + 0.375, false)
							&& SetPenWidthIfRevision(brushPreset.resolvedWidthDip, StateModeTransitionRevision(), false),
							"bar-shape-explicit-release-commit");
						const auto explicitSnapshot = GetStateModeSnapshot();
						selectedOnly(explicitSnapshot, static_cast<int>(index));
						const auto untouched = shape == ShapeModeSelectEnum::IdtShapeRectangle1
							? explicitSnapshot.Shape.StraightLine1.widthSelection : explicitSnapshot.Shape.Rectangle1.widthSelection;
						require(GetPenWidthSelection(explicitSnapshot).source == Source::ExplicitDip
							&& explicitSnapshot.Pen.Brush1.widthSelection.explicitDip == 9.25
							&& untouched.source == otherSelection.source && untouched.preset == otherSelection.preset
							&& untouched.explicitDip == otherSelection.explicitDip,
							"bar-shape-highlight-preserves-independent-slots");
					}
				}
			}

			stage = "preset-highlight-precision-and-invalid-inputs";
			ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
			auto matching = GetStateModeSnapshot();
			matching.penWidthContext = highlightContexts[0];
			matching.Pen.Brush1.widthSelection = { Source::ExplicitDip, Preset::Medium, 3.0 };
			selectedOnly(matching, 1);
			matching.Pen.Brush1.widthSelection.explicitDip = std::nextafter(3.0, 4.0);
			selectedOnly(matching, 1);
			for (int step = 0; step < 16; ++step)
				matching.Pen.Brush1.widthSelection.explicitDip = std::nextafter(
					matching.Pen.Brush1.widthSelection.explicitDip, 4.0);
			selectedOnly(matching, -1);
			matching.Pen.Brush1.widthSelection.explicitDip = static_cast<double>(std::nextafter(3.0f, 4.0f));
			selectedOnly(matching, -1);
			matching.Pen.Brush1.widthSelection.explicitDip = 3.1;
			require(Inkeys::UI::Bar::Detail::FormatWidthDipNumber(3.1)
				== Inkeys::UI::Bar::Detail::FormatWidthDipNumber(3.0),
				"bar-same-rounded-text-fixture");
			selectedOnly(matching, -1);
			matching.penWidthContext = highlightContexts[4];
			matching.Pen.Brush1.widthSelection.explicitDip = GetBarThicknessPresetMetrics(
				PenModeSelectEnum::IdtPenSoftPen, 1, matching).resolvedWidthDip;
			selectedOnly(matching, 1);
			matching.Pen.Brush1.widthSelection = { Source::Preset, Preset::Thick,
				matching.Pen.Brush1.widthSelection.explicitDip };
			selectedOnly(matching, 2);
			// 用真实策略触发异常全幅尺寸的同值回退，显式值只选首档，身份仍优先。
			matching.penWidthContext = { 56, 0, 96.0,
				(std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)() };
			for (const auto mode : { PenModeSelectEnum::IdtPenSoftPen, PenModeSelectEnum::IdtPenHighlighter1 })
			{
				matching.Pen.ModeSelect = mode;
				const double fallbackDip = GetBarThicknessPresetMetrics(mode, 0, matching).resolvedWidthDip;
				require(GetBarThicknessPresetMetrics(mode, 1, matching).resolvedWidthDip == fallbackDip
					&& GetBarThicknessPresetMetrics(mode, 2, matching).resolvedWidthDip == fallbackDip,
					"bar-duplicate-fallback-dip-fixture");
				auto& selection = mode == PenModeSelectEnum::IdtPenHighlighter1
					? matching.Pen.Highlighter1.widthSelection : matching.Pen.Brush1.widthSelection;
				selection = { Source::ExplicitDip, Preset::Thick, fallbackDip };
				selectedOnly(matching, 0);
				require(selection.source == Source::ExplicitDip && selection.explicitDip == fallbackDip,
					"bar-fallback-highlight-preserves-explicit-dip");
				selection = { Source::Preset, Preset::Thick, fallbackDip };
				selectedOnly(matching, 2);
			}
			matching.Pen.ModeSelect = PenModeSelectEnum::IdtPenSoftPen;
			matching.penWidthContext = { 55, 0, 1536.0, 1, 1 };
			matching.Pen.Brush1.widthSelection = { Source::ExplicitDip, Preset::Thin,
				GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen, 0, matching).resolvedWidthDip };
			selectedOnly(matching, 0);
			for (int step = 0; step < 64; ++step)
				matching.Pen.Brush1.widthSelection.explicitDip = std::nextafter(
					matching.Pen.Brush1.widthSelection.explicitDip, 1.0);
			selectedOnly(matching, -1); // 小于 1 DIP 时也不能添加绝对容差吸附。
			for (const double invalid : { 0.0, -1.0, MaximumDip + 1.0,
				std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
			{
				matching.Pen.Brush1.widthSelection.explicitDip = invalid;
				selectedOnly(matching, -1);
			}
			matching.Pen.Brush1.widthSelection = { static_cast<Source>(99), Preset::Medium, 3.0 };
			selectedOnly(matching, -1);
			matching.Pen.Brush1.widthSelection = { Source::Preset, static_cast<Preset>(99), 3.0 };
			selectedOnly(matching, -1);
			require(!IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen, 3, 1.0, matching),
				"bar-invalid-preset-index-not-selected");

			stage = "explicit-highlight-cancel-stale-and-display-change";
			setlist.paintDevice = 1;
			ApplyPenWidthContext(highlightContexts[0]);
			require(SetPenWidth(7.0, false), "bar-cancel-nonpreset-seed");
			begin(7.0);
			session.thicknessFineDialRawValue = 6.0;
			session.PublishThicknessFineDialCandidate();
			session.CancelThicknessFineDialSelection();
			session.CommitThicknessFineDialSelection(false);
			require(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip == 7.0,
				"bar-cancel-does-not-commit-matching-candidate");
			selectedOnly(GetStateModeSnapshot(), -1);
			begin(7.0);
			session.thicknessFineDialRawValue = 6.0;
			session.PublishThicknessFineDialCandidate();
			ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
			session.CommitThicknessFineDialSelection(false);
			selectedOnly(GetStateModeSnapshot(), -1);
			require(SetPenWidth(3.0, false), "bar-display-explicit-matching-seed");
			selectedOnly(GetStateModeSnapshot(), 1);
			setlist.paintDevice = 0;
			ApplyPenWidthContext(highlightContexts[3]);
			const auto afterDisplay = GetStateModeSnapshot();
			require(GetPenWidthSelection(afterDisplay).source == Source::ExplicitDip
				&& GetPenWidthMetrics(afterDisplay).resolvedWidthDip == 3.0,
				"bar-display-change-keeps-explicit-dip");
			selectedOnly(afterDisplay, -1);
			setlist.paintDevice = originalPaintDevice;
			ApplyPenWidthContext(originalContext);
			ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
			require(SetPenWidth(7.0, false), "bar-highlight-restore-existing-fixture");

			stage = "touch-preview-candidate-same-frame";
			for (const auto tool : { PenToolSelectionEnum::SoftPen, PenToolSelectionEnum::HardPen,
				PenToolSelectionEnum::Highlighter, PenToolSelectionEnum::Laser })
			{
				ChangeStateModeToPenTool(tool);
				require(SetPenWidth(7.0, false), "touch-preview-seed-width");
				const auto beforeTouch = GetStateModeVersionedSnapshot();
				const auto beforeTouchBridge = bridgeRevision();
				const auto touchRange = GetBarThicknessSliderRange(
					beforeTouch.state.Pen.ModeSelect, 1.0, beforeTouch.state);
				// 激光不开放滑轨；仍验证其已有候选预览共用同一单次转换与芯宽规则。
				const BarThicknessSliderRange candidateRange = touchRange.supported
					? touchRange : BarThicknessSliderRange{ 1.0, 30.0, true };
				shared.thicknessSliderCandidateWidth = 7.0;
				shared.thicknessPreviewDragging = true;
				require(session.PublishThicknessSliderCandidate(12.25, candidateRange, true),
					"touch-preview-real-session-publishes-candidate");
				auto previewSnapshot = beforeTouch.state;
				previewSnapshot.penWidthContext = { 44, 1, 144.0, 1920, 1080 };
				BarUiValueClass core{ 7.0 }, outer{ 7.0 };
				for (const double candidate : { 12.25, 11.75 })
				{
					session.PublishThicknessSliderCandidate(candidate, candidateRange, true);
					const auto live = GetBarThicknessPreviewMetrics(previewSnapshot,
						shared.thicknessPreviewDragging, shared.thicknessSliderCandidateWidth);
					const double expectedPx = candidate == 12.25 ? 18.375 : 17.625;
					UpdateBarThicknessPreviewWidth(core,
						previewSnapshot.laserActive ? live.appliedCanvasWidthPx / 3.0
							: live.appliedCanvasWidthPx, shared.thicknessPreviewDragging, 0.2);
					UpdateBarThicknessPreviewWidth(outer, live.appliedCanvasWidthPx,
						shared.thicknessPreviewDragging, 0.2);
					require(live.resolvedWidthDip == candidate && outer.val == expectedPx
						&& core.val == (previewSnapshot.laserActive ? expectedPx / 3.0 : expectedPx)
						&& core.IsSame() && outer.IsSame(),
						"touch-preview-pixels-use-current-candidate-without-animation-tick");
					require(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip == 7.0
						&& StateModeTransitionRevision() == beforeTouch.revision
						&& bridgeRevision() == beforeTouchBridge,
						"touch-preview-candidate-does-not-write-business-or-bridge");
				}
				shared.thicknessSliderHoldLocked = true;
				require(!session.PublishThicknessSliderCandidate(20.0, candidateRange, true)
					&& shared.thicknessSliderCandidateWidth == 11.75,
					"touch-preview-hold-lock-preserves-candidate");
				::barUISet.CollapseAuxiliaryPanels(true);
				session.barState.drawAttribute = true;
				require(!shared.thicknessPreviewDragging && shared.thicknessSliderCandidateWidth == 0.0
					&& GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip == 7.0
					&& bridgeRevision() == beforeTouchBridge,
					"touch-preview-public-panel-close-cancels-without-commit");
				const auto cancelled = GetBarThicknessPreviewMetrics(beforeTouch.state,
					shared.thicknessPreviewDragging, shared.thicknessSliderCandidateWidth);
				require(cancelled.resolvedWidthDip == 7.0,
					"touch-preview-cancel-restores-authoritative-width");
				UpdateBarThicknessPreviewWidth(outer, cancelled.appliedCanvasWidthPx, false, 0.2);
				require(outer.tar == cancelled.appliedCanvasWidthPx && !outer.IsSame(),
					"touch-preview-non-interaction-keeps-transition-animation");
				require(SetPenWidthIfRevision(11.75, beforeTouch.revision, false),
					"touch-preview-release-uses-existing-revision-commit");
				require(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip == 11.75
					&& !SetPenWidthIfRevision(12.25, beforeTouch.revision, false),
					"touch-preview-commit-keeps-fraction-and-rejects-stale-revision");
			}
			ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
			require(SetPenWidth(7.0, false), "touch-preview-restore-existing-fixture");
			shared.thicknessViewMode = ThicknessViewMode::FineDial;

			stage = "fine-dial-stale-physics";
			begin(9.0);
			session.EndThicknessFineDialDrag(false);
			require(shared.thicknessFineDialPhysicsActive, "fine-dial-physics-started");
			ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
			const auto afterSwitch = bridgeRevision();
			session.AdvanceThicknessFineDialPhysics();
			require(cleared() && GetPenWidth() == 7.0f && bridgeRevision() == afterSwitch,
				"fine-dial-old-physics-cancelled-without-write");

			stage = "fine-dial-cross-generation-begin";
			begin(14.0);
			// 用真实采样环进入惯性，固定输入时间戳避免为交错验证增加等待。
			session.ResetThicknessFineDialSamples();
			session.AddThicknessFineDialSample(100.0, session.thicknessFineDialGrabTick);
			session.AddThicknessFineDialSample(84.0, session.thicknessFineDialGrabTick + 16);
			session.EndThicknessFineDialDrag(false);
			require(session.thicknessFineDialPhase == ThicknessFineDialPhase::Inertia &&
				session.thicknessFineDialVelocity != 0.0, "fine-dial-old-inertia-established");
			ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
			const auto newRevision = StateModeTransitionRevision();
			begin(7.0);
			require(session.thicknessFineDialModeRevision == newRevision &&
				session.thicknessFineDialResidualVelocity == 0.0 &&
				session.thicknessFineDialRawValue == 7.0 &&
				shared.thicknessFineDialVisualWidth == 7.0f,
				"fine-dial-new-generation-does-not-inherit-old-candidate");

			stage = "fine-dial-cancel-then-commit";
			session.CancelThicknessFineDialSelection();
			begin(11.0);
			session.CancelThicknessFineDialSelection();
			require(cleared(), "fine-dial-cancel-clears-before-no-save-commit");
			const auto beforeCancelledCommit = bridgeRevision();
			// 只走取消/失效提交，不能在未初始化配置路径执行成功提交的 SetMemory。
			session.CommitThicknessFineDialSelection();
			require(cleared() && GetPenWidth() == 7.0f && bridgeRevision() == beforeCancelledCommit,
				"fine-dial-cancelled-commit-no-write");

			stage = "fine-dial-stale-commit";
			begin(12.0);
			ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
			const auto highlighterWidth = GetPenWidth();
			const auto beforeStaleCommit = bridgeRevision();
			session.CommitThicknessFineDialSelection();
			require(cleared() && GetPenWidth() == highlighterWidth && bridgeRevision() == beforeStaleCommit,
				"fine-dial-stale-commit-no-write");
			require(!ProductRunning() && !Inkeys::Window::GetService().Running(), "fine-dial-stays-isolated");
			std::fputs("[StateModeProduction] PASS: FineDial stale physics, cross-generation inertia, cancel and stale commit\n", stderr);
			return 0;
		}
		catch (const char* check)
		{
			std::fprintf(stderr, "[StateModeProduction] FAIL: stage=%s check=%s\n", stage, check);
		}
		catch (...)
		{
			std::fprintf(stderr, "[StateModeProduction] FAIL: stage=%s exception\n", stage);
		}
		return 1;
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
			case IndependentHoverTargetEnum::DrawAttributeSoftPen:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_SoftPen]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_SoftPen]->fill.value(),
					&drawAttributeSoftPenHoverStage };
			case IndependentHoverTargetEnum::DrawAttributeLaser:
				return { &shapeMap[BarUISetShapeEnum::DrawAttributeBar_Laser]->pct,
					&shapeMap[BarUISetShapeEnum::DrawAttributeBar_Laser]->fill.value(),
					&drawAttributeLaserHoverStage };
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
			if (Inkeys::UI::Bar::StartBarButtonHoverVisual(hoverPct, hoverFill, hoverStage))
				UpdateRendering(false);
		}

	void StopHover(BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtonHoverStageEnum>* hoverStage, bool immediate,
		bool preserveVisual = false)
		{
			if (Inkeys::UI::Bar::StopBarButtonHoverVisual(
				hoverPct, hoverFill, hoverStage, immediate, preserveVisual))
				UpdateRendering(false);
		}

	void StartMainBarButtonHover(BarButtonClass* button)
		{
			if (button && StartBarButtonHoverVisual(*button))
				UpdateRendering(false);
		}

	void StopMainBarButtonHover(
		BarButtonClass* button, bool immediate, bool preserveVisual = false)
		{
			if (!button) return;
			if (StopBarButtonHoverVisual(
				*button, immediate, preserveVisual)) UpdateRendering(false);
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
			if (ReadBarPointerPosition(&point))
			{
				hoverSuppressionScreenPoint = point;
				suppressHoverUntilPointerMove = true;
			}
		}

	bool ThicknessSliderAvailable()
		{
			return stateMode.StateModeSelect
				== StateModeSelectEnum::IdtPen
				&& !stateMode.laserActive
				&& barState.drawAttribute && !barState.fold
				&& GetBarThicknessSliderRange(
					stateMode.Pen.ModeSelect,
					barStyle.dpiZoom, stateMode).supported;
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

	bool PublishThicknessSliderCandidate(double targetWidth,
		const BarThicknessSliderRange& range, bool candidateAllowed)
	{
		if (!candidateAllowed || barState.drawAttributeBar.thicknessSliderHoldLocked
			|| !Inkeys::Business::PenWidth::IsValidDip(targetWidth)) return false;
		const double candidate = clamp(targetWidth, range.min, range.max);
		if (abs(candidate - static_cast<double>(barState.drawAttributeBar
			.thicknessSliderCandidateWidth)) <= 0.000001) return false;
		barState.drawAttributeBar.thicknessSliderCandidateWidth = candidate;
		return true;
	}

	double PublishThicknessFineDialCandidate()
		{
			thicknessFineDialVisualValue =
				ProjectThicknessFineDialRubberBand(
					thicknessFineDialRawValue);
			double candidate = Inkeys::UI::Bar::Detail::StepFineDialDip(
				thicknessFineDialOriginDip, thicknessFineDialVisualValue,
				thicknessFineDialRangeMin, thicknessFineDialRangeMax);
			barState.drawAttributeBar.thicknessFineDialVisualWidth =
				thicknessFineDialVisualValue;
			barState.drawAttributeBar.thicknessSliderCandidateWidth =
				candidate;
			barState.drawAttributeBar.thicknessFineDialCandidateActive = true;
			thicknessFineDialDirty = thicknessFineDialDirty
				|| abs(candidate - thicknessFineDialOriginDip) > 0.000001;
			return candidate;
		}

	void CancelThicknessFineDialSelection()
		{
			thicknessFineDialModeRevision = 0;
			thicknessFineDialPhase = ThicknessFineDialPhase::Idle;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialResidualVelocity = 0.0;
			thicknessFineDialSettleTarget = 0.0;
			thicknessFineDialCommitIssued = false;
			thicknessFineDialDirty = false;
			ResetThicknessFineDialSamples();
			barState.drawAttributeBar.thicknessFineDialDragging = false;
			barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
			barState.drawAttributeBar.thicknessFineDialCandidateActive = false;
			barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
			barState.drawAttributeBar.thicknessSliderHoldHintActive = false;
			barState.drawAttributeBar.thicknessSliderHoldLocked = false;
			barState.drawAttributeBar.thicknessSliderHoldProgress = 0.0f;
		}

	std::uint64_t CommitThicknessFineDialSelection(bool setMemory = true)
		{
			bool candidateActive = barState.drawAttributeBar
				.thicknessFineDialCandidateActive;
			double candidate = Inkeys::UI::Bar::Detail::StepFineDialDip(
				thicknessFineDialOriginDip, thicknessFineDialRawValue,
				thicknessFineDialRangeMin, thicknessFineDialRangeMax);
			bool shouldCommit = candidateActive
				&& thicknessFineDialDirty
				&& !thicknessFineDialCommitIssued
				&& thicknessFineDialModeRevision == StateModeTransitionRevision()
				&& abs(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip - candidate)
					> 0.000001;
			thicknessFineDialCommitIssued = true;
			thicknessFineDialPhase = ThicknessFineDialPhase::Idle;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialResidualVelocity = 0.0;
			ResetThicknessFineDialSamples();
			barState.drawAttributeBar.thicknessFineDialDragging = false;
			barState.drawAttributeBar.thicknessFineDialPhysicsActive = false;
			std::uint64_t committedRevision = thicknessFineDialModeRevision;
			if (shouldCommit)
			{
				if (SetPenWidthIfRevision(candidate, thicknessFineDialModeRevision, setMemory))
					++committedRevision;
				else committedRevision = 0;
			}
			if (StateModeTransitionRevision() != committedRevision) committedRevision = 0;
			barState.drawAttributeBar.thicknessFineDialCandidateActive = false;
			barState.drawAttributeBar.thicknessSliderCandidateWidth = 0.0f;
			thicknessFineDialModeRevision = 0;
			thicknessFineDialCommitIssued = false;
			return committedRevision;
		}

	void BeginThicknessFineDialDrag(double startValue,
		double screenX, double unitTravelScreen,
		const BarThicknessSliderRange& range, std::uint64_t modeRevision,
		bool inheritedAdjustment = false)
		{
			bool continuingMotion = thicknessFineDialPhase
				== ThicknessFineDialPhase::Inertia
				|| thicknessFineDialPhase
					== ThicknessFineDialPhase::Settling;
			const bool sameModeRevision =
				thicknessFineDialModeRevision == modeRevision;
			if (continuingMotion && !sameModeRevision)
				CancelThicknessFineDialSelection();
			thicknessFineDialResidualVelocity =
				sameModeRevision && thicknessFineDialPhase == ThicknessFineDialPhase::Inertia
					? thicknessFineDialVelocity : 0.0;
			if (continuingMotion && sameModeRevision
				&& barState.drawAttributeBar.thicknessFineDialCandidateActive)
				startValue = static_cast<double>(barState.drawAttributeBar
					.thicknessFineDialVisualWidth);
			thicknessFineDialModeRevision = modeRevision;
			thicknessFineDialRangeMin = range.min;
			thicknessFineDialRangeMax = range.max;
			thicknessFineDialUnitTravelScreen = max(0.000001,
				unitTravelScreen);
			if (!continuingMotion || !sameModeRevision)
			{
				thicknessFineDialOriginDip = startValue;
				thicknessFineDialDirty = inheritedAdjustment;
			}
			else thicknessFineDialDirty = thicknessFineDialDirty || inheritedAdjustment;
			thicknessFineDialRawValue = startValue;
			thicknessFineDialVelocity = 0.0;
			thicknessFineDialGrabTick = BarPointerSampleTick();
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
					Inkeys::UI::Bar::Detail::StepFineDialDip(
						thicknessFineDialOriginDip, thicknessFineDialRawValue,
						thicknessFineDialRangeMin, thicknessFineDialRangeMax),
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
			stateMode = GetStateModeSnapshot();
			if (thicknessFineDialModeRevision != StateModeTransitionRevision())
			{
				// 旧工具的惯性不得落到 PPT 或用户刚选中的新笔型。
				CancelThicknessFineDialSelection();
				UpdateRendering(false);
				return;
			}
			if (!barState.drawAttributeBar.thicknessFineDialPhysicsActive
				|| barState.drawAttributeBar.thicknessViewMode
					!= ThicknessViewMode::FineDial
				|| !ThicknessSliderAvailable())
			{
				CancelThicknessFineDialSelection();
				return;
			}
			auto range = GetBarThicknessSliderRange(
				stateMode.Pen.ModeSelect, barStyle.dpiZoom, stateMode);
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
						Inkeys::UI::Bar::Detail::StepFineDialDip(
						thicknessFineDialOriginDip, thicknessFineDialRawValue,
						thicknessFineDialRangeMin, thicknessFineDialRangeMax),
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
					return !IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen,
						index, barStyle.dpiZoom, stateMode);
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
				return stateMode.laserActive
					|| stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHardPen;
			case IndependentHoverTargetEnum::DrawAttributeSoftPen:
				return stateMode.laserActive
					|| stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenSoftPen;
			case IndependentHoverTargetEnum::DrawAttributeLaser:
				// 和其他笔型按钮一致：未选中时显示悬停层，选中时由选中态背景接管。
				return !stateMode.laserActive;
			case IndependentHoverTargetEnum::DrawAttributeHighlight:
				return stateMode.laserActive
					|| stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1;
			case IndependentHoverTargetEnum::DrawAttributePenTypeExtension:
				return !barState.fold
					&& !stateMode.laserActive
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect)
					&& !barState.drawAttributeBar.penTypeExtensionPress;
			case IndependentHoverTargetEnum::DrawAttributePenTypeFreeLine:
				return !barState.fold
					&& !stateMode.laserActive
					&& barState.drawAttributeBar.penTypeMenuOpen
					&& PenModeSupportsAnnotationLine(
						stateMode.Pen.ModeSelect)
					&& !barState.drawAttributeBar.penTypeFreeLinePress;
case IndependentHoverTargetEnum::DrawAttributeThicknessFine:
				case IndependentHoverTargetEnum::DrawAttributeThicknessMedium:
				case IndependentHoverTargetEnum::DrawAttributeThicknessCoarse:
				{
					bool laserPresetMode = IsLaserThicknessPresetMode(stateMode);
					if (!laserPresetMode
						&& !PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect, stateMode))
						return false;
					size_t index = static_cast<size_t>(target)
						- static_cast<size_t>(
							IndependentHoverTargetEnum::DrawAttributeThicknessFine);
					return !IsBarThicknessPresetSelected(
						stateMode.Pen.ModeSelect, index, barStyle.dpiZoom, stateMode);
				}
			case IndependentHoverTargetEnum::DrawAttributeThicknessAdjust:
				return ThicknessSliderAvailable()
					&& barState.drawAttributeBar.thicknessViewMode
						== ThicknessViewMode::Preview;
			case IndependentHoverTargetEnum::DrawAttributeAnnotationClose:
				return !barState.fold
					&& barState.drawAttributeBar.penTypeMenuOpen
					&& !stateMode.laserActive
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
			&& !stateMode.laserActive
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

			PrepareBarMouseDragTracking();
			BeginBarMouseDrag(); // 选色板按下即进入连续选色，不借用普通按钮的点击提交。
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

			POINT stablePoint = BarPointerScreenPoint(gestureMessage);
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
				if (!ColorPickerAvailable() || !picker.colorPickerOpen)
					break;
				// 触摸不会保证同步系统鼠标位置，静止期间沿用最后一个触点坐标。
				UpdateHold(activeScreenPoint);
				std::this_thread::sleep_for(std::chrono::milliseconds(8));
				continue;
			}
			if (IsBarPointerCancelMessage(gestureMessage)
				|| !ColorPickerAvailable() || !picker.colorPickerOpen) break;
			if (gestureMessage.message == WM_MOUSEMOVE && gestureMessage.lbutton)
			{
				POINT screenPoint = BarPointerScreenPoint(gestureMessage);
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
		bool completed = !offSignal
			&& !IsBarPointerCancelMessage(gestureMessage)
			&& gestureMessage.message == WM_LBUTTONUP && !gestureMessage.lbutton && ColorPickerAvailable()
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
			FinishBarPointerSession();
			barState.drawAttributeBar.laserPress = false;
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
			if (barUISet.eraserAttribute.Keyboard(barUISet, msg.vkcode,
				msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN))
				return BarInteractionStageResult::Consumed;
			if (barUISet.recallAttribute.Keyboard(barUISet, msg.vkcode,
				msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN))
				return BarInteractionStageResult::Consumed;
			HandleColorPickerKeyboard(msg);
			return BarInteractionStageResult::Consumed;
		}
		return BarInteractionStageResult::PassThrough;
		}

	BarInteractionStageResult HandleBottomDockIndicatorOcclusion()
	{
		if (!IsBarCoordinateMessage(msg.message))
			return BarInteractionStageResult::PassThrough;
		LONG visualX = msg.x;
		LONG visualY = msg.y;
		if (!IsBarTouchScreenMessage(msg))
		{
			visualX = barUISet.BottomDockRigidVisualX(msg.x);
			visualY = barUISet.BottomDockRigidVisualY(msg.y);
		}
		if (!barUISet.IsBottomDockIndicatorPresentedAt(visualX, visualY))
			return BarInteractionStageResult::PassThrough;
		if (Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
		{
			Inkeys::UI::Bar::RejectAuthorizedFixtureSource(Inkeys::UI::Bar::Ui3FixtureSourceFailure::Action);
			return BarInteractionStageResult::Shutdown;
		}

		// 只消费成功呈现的指示器像素，并同步撤销下层控件候选状态。
		if (hoveredMainBarButton)
		{
			StopMainBarButtonHover(hoveredMainBarButton, true);
			hoveredMainBarButton = nullptr;
		}
		if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
		{
			StopIndependentHover(hoveredIndependentButton, true);
			hoveredIndependentButton = IndependentHoverTargetEnum::None;
		}
		for (int id = 0; id < barButtonSet.tot; ++id)
		{
			auto button = barButtonSet.buttonList.Get(id);
			if (button && button->state
				&& button->state->emph == BarWidgetEmphasize::Pressed)
				SetBarButtonPressedVisual(*button, false);
		}
		lastClickedMainBarButton = nullptr;
		auto& draw = barState.drawAttributeBar;
		draw.brush1Press = false;
		draw.softPenPress = false;
		draw.laserPress = false;
		draw.highlight1Press = false;
		draw.penTypeExtensionPress = false;
		draw.penTypeFreeLinePress = false;
		draw.thicknessFinePress = false;
		draw.thicknessMediumPress = false;
		draw.thicknessCoarsePress = false;
		draw.thicknessAdjustPress = false;
		draw.thicknessSliderHover = false;
		draw.thicknessSliderPressed = false;
		draw.thicknessSliderDragging = false;
		draw.thicknessPreviewDragging = false;
		draw.thicknessSliderCapture = false;
		draw.thicknessFineDialDragging = false;
		draw.thicknessAnnotationHover = false;
		draw.thicknessAnnotationHoverGrace = false;
		draw.thicknessAnnotationClosePress = false;
		draw.thicknessOverflowHover = false;
		draw.thicknessOverflowHoverGrace = false;
		draw.thicknessOverflowClosePress = false;
		draw.colorPickerTonePress = false;
		draw.colorPickerClosePress = false;
		draw.colorPickerPointerPressed = false;
		draw.colorPickerPointerCapture = false;
		barState.moreClosePress = false;
		barState.geometryAttributeBar.straightLinePress = false;
		barState.geometryAttributeBar.rectanglePress = false;
		barState.geometryAttributeBar.thicknessFinePress = false;
		barState.geometryAttributeBar.thicknessMediumPress = false;
		barState.geometryAttributeBar.thicknessCoarsePress = false;
		barState.geometryAttributeBar.closePress = false;
		if (GetCapture() == floating_window) ReleaseCapture();
		UpdateRendering(false);
		return BarInteractionStageResult::Consumed;
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
				if (ReadBarPointerPosition(&currentPoint)
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
				// 普通主栏的双轴命中必须来自同一个成功呈现 tuple。
				const auto mainBodyPresented =
					barUISet.BottomDockPresentedSnapshot();
				const int mainBodyVisualX = static_cast<int>(lround(
					static_cast<double>(msg.x)
						+ mainBodyPresented.horizontalMapping
							.rigidOverlayTranslationXDip
							* mainBodyPresented.zoom));
				const int mainBodyVisualY = static_cast<int>(lround(
					static_cast<double>(msg.y)
						+ mainBodyPresented.rigidTranslationDip
							* mainBodyPresented.zoom));
				const int mainBodyHitTestX = static_cast<int>(lround(
					Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelX(
						static_cast<double>(mainBodyVisualX),
						mainBodyPresented.horizontalMapping,
						mainBodyPresented.zoom)));
				const int mainBodyHitTestY = static_cast<int>(lround(
					Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelY(
						static_cast<double>(mainBodyVisualY),
						mainBodyPresented.mapping,
						mainBodyPresented.zoom)));
				if (!barState.fold && !colorPickerOccludes
					&& !penTypeMenuOccludes)
				{
					for (int id = 0; id < barButtonSet.tot; id++)
					{
						BarButtonClass* temp = barButtonSet.buttonList.Get(id);
						if (!temp || !temp->IsVisible()
							|| temp->preset == BarButtonPresetEnum::Divider
							|| temp->state->state == BarWidgetState::Disable
							|| temp->state->state == BarWidgetState::Selected) continue;
						bool isColorSelector = temp->name.enable.tar
							&& temp->name.content.GetTar().substr(0, 7) == L"__color";
						if (isColorSelector) continue; // 颜色块自身就是内容，不把其填充色改成悬停灰色。
						if (temp->button.IsClick(
							mainBodyHitTestX, mainBodyHitTestY, barStyle.zoom))
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
										|| button->state->state == BarWidgetState::Disable
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
					if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Laser];
						obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeLaser;
					}
					else if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1];
					stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHardPen
					&& obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeBrush;
					}
					else if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_SoftPen];
						stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenSoftPen
						&& obj && obj->IsClick(msg.x, msg.y, barStyle.zoom))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeSoftPen;
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
							if (IsBarPointerCancelMessage(msg)) break;
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
							if (IsBarPointerCancelMessage(msg)) break;
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
							if (IsBarPointerCancelMessage(msg)) break;
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
						if (IsBarPointerCancelMessage(msg)) break;
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
						if (IsBarPointerCancelMessage(msg)) break;
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
							if (IsBarPointerCancelMessage(msg)) break;
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
							if (IsBarPointerCancelMessage(msg)) break;
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
							|| button->state->state == BarWidgetState::Disable
							|| !button->button.IsClick(msg.x, msg.y, barStyle.zoom)) continue;
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN)
						{
							SetBarButtonPressedVisual(*button, true);
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
								if (IsBarPointerCancelMessage(msg)) break;
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
							SetBarButtonPressedVisual(*button, false);
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
			const short rigidMessageX = msg.x;
			const short rigidMessageY = msg.y;
			int visualMessageX = rigidMessageX;
			int visualMessageY = rigidMessageY;
			ExMessage gripMessage = msg;
			POINT gripVisualPoint{};
			const POINT gripLogicalPoint =
				barUISet.BottomDockGripHitTestPointFromRigid(
					POINT{ rigidMessageX, rigidMessageY }, &gripVisualPoint);
			gripMessage.x = static_cast<short>(clamp(
				static_cast<int>(gripLogicalPoint.x),
				static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
			gripMessage.y = static_cast<short>(clamp(
				static_cast<int>(gripLogicalPoint.y),
				static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX)));
			visualMessageX = gripVisualPoint.x;
			visualMessageY = gripVisualPoint.y;

			// 普通主栏控件继续使用主体逆映射，主按钮单独使用刚性抓手逆映射。
			ApplyBarBottomDockBodyHitTestFromRigid(
				msg, nullptr, nullptr);

			// 主按钮
			if (auto obj = superellipseMap[BarUISetSuperellipseEnum::MainButton]; continueFlag && obj->IsClick(gripMessage.x, gripMessage.y, barStyle.zoom))
			{
				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(Inkeys::UI::Bar::Ui3FiniteScene::MainFold,
						Inkeys::UI::Bar::Ui3FixtureActionPoint::Down)) return BarInteractionStageResult::Shutdown;
					// Seek 需要按下时的真实视觉坐标，不能把主体逆形变坐标当作抓取点。
					ExMessage seekMessage = msg;
					seekMessage.x = static_cast<short>(clamp(
						visualMessageX, static_cast<int>(SHRT_MIN),
						static_cast<int>(SHRT_MAX)));
					seekMessage.y = static_cast<short>(clamp(
						visualMessageY, static_cast<int>(SHRT_MIN),
						static_cast<int>(SHRT_MAX)));
					const BarSeekResult seekResult = Seek(seekMessage);
					if (seekResult.allowClick)
					{
						if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(Inkeys::UI::Bar::Ui3FiniteScene::MainFold,
							Inkeys::UI::Bar::Ui3FixtureActionPoint::Commit)) return BarInteractionStageResult::Shutdown;
						// 奇数publication必须先于coalescer及第一业务写，默认无probe不工作。
						Inkeys::UI::Bar::Ui3FiniteMutationScope finiteMutation(Inkeys::UI::Bar::Ui3FiniteScene::MainFold);
						if (barUISet.TryBeginToggle(BarToggleChannel::Main))
						{
							finiteMutation.MarkBusinessAccepted();
							mainButtonClickPulseSerial.fetch_add(
								1, std::memory_order_relaxed);
							finiteMutation.ObserveBusinessWrite();
							// 展开/收起主栏
							if (barState.fold)
							{
								barState.fold = false;
								// 展开完成后由渲染线程按最终联合外框无提示判定自动居中。
								barUISet.RequestBottomDockCenterAfterExpand();
							}
							else
							{
								barState.fold = true;
								barUISet.ClearBottomDockCenterForFold();
								Inkeys::UI::Bar::ClearWhiteboardDockLock();
								barState.moreExpanded = false;
								CloseThicknessSlider(true);
								CloseColorPicker(true);
							}
							UpdateRendering();
						}
						else finiteMutation.FinishRejected(Inkeys::UI::Bar::Ui3FiniteStatus::RejectedByBusiness,
							Inkeys::UI::Bar::Ui3BusinessWriteWitness::NoBusinessWrite);
					}
					SuppressHoverUntilPointerMove();

					ClearBarInteractionMessages(EM_MOUSE, floating_window);
				}
				if (msg.message == WM_RBUTTONDOWN && setlist.RightClickClose)
				{
					const auto title = I18n::getWOr(
						I18nKey.Dialogs.Common.TipsTitle, L"Inkeys Tips");
					const auto body = I18n::getWOr(
						I18nKey.Dialogs.CloseInkeys.Body, L"Close Inkeys?");
					const auto okLabel = I18n::getWOr(
						I18nKey.Dialogs.Common.OK, L"OK");
					const auto cancelLabel = I18n::getWOr(
						I18nKey.Dialogs.Common.Cancel, L"Cancel");
					auto request = Inkeys::UI::MessageBox::MakeOkCancelRequest(
						title.c_str(), body.c_str());
					request.language = I18n::languageId();
					request.labels.ok = okLabel.c_str();
					request.labels.cancel = cancelLabel.c_str();
					request.owner = floating_window;
					request.requireOwner = true;
					request.fallback.owner = floating_window;
					request.fallback.modality =
						Inkeys::UI::MessageBox::SystemModality::System;
					if (Inkeys::UI::MessageBox::Show(request)
						== Inkeys::UI::MessageBox::Result::Ok) CloseProgram();

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
						|| temp->preset == BarButtonPresetEnum::Divider
						|| temp->state->state == BarWidgetState::Disable) continue;

					// 双击第二击仍归属于第一击按钮，避免动画中按钮位移导致命中丢失。
					bool doubleClickContinuation = msg.message == WM_LBUTTONDBLCLK
						&& temp == lastClickedMainBarButton;
					if (temp->button.IsClick(msg.x, msg.y, barStyle.zoom) || doubleClickContinuation)
					{
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK)
						{
							const auto fixtureAction = temp->preset == BarButtonPresetEnum::Draw
								? Inkeys::UI::Bar::Ui3FiniteScene::DrawAttribute : static_cast<Inkeys::UI::Bar::Ui3FiniteScene>(0);
							if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(fixtureAction,
								Inkeys::UI::Bar::Ui3FixtureActionPoint::Down)) return BarInteractionStageResult::Shutdown;
							bool clickCompleted = false;
							// 同一背景层先切换到按下状态；抬起后必须收到新的鼠标移动才能再次悬停。
							SetBarButtonPressedVisual(*temp, true);
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
								if (IsBarPointerCancelMessage(msg)) break;
								ApplyBarBottomDockBodyHitTestFromRigid(msg);
								if (doubleClickContinuation || temp->button.IsClick(msg.x, msg.y, barStyle.zoom))
								{
									// Move 缺少 MK_LBUTTON 不能代表抬起，点击只在明确 Up 后执行。
									if (msg.message == WM_LBUTTONUP && !msg.lbutton)
									{
										if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(fixtureAction,
											Inkeys::UI::Bar::Ui3FixtureActionPoint::Commit)) return BarInteractionStageResult::Shutdown;
										Inkeys::UI::Bar::Ui3FiniteMutationScope finiteMutation(
											Inkeys::UI::Bar::Ui3FiniteScene::DrawAttribute, temp->preset == BarButtonPresetEnum::Draw);
										ClosePenTypeMenu();
										// ClosePenTypeMenu确有写入，后续拒绝不能假称NoBusinessWrite。
										finiteMutation.ObserveBusinessWrite();
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
										else if (temp->preset == BarButtonPresetEnum::Clean)
											barButtonSet.ExecuteClearClick(doubleClickContinuation);
										else if (temp->clickFunc) temp->clickFunc();
										// 仅业务动作交还焦点；打开设置/颜色等编辑入口不抢走输入。
										switch (temp->preset.load())
										{
										case BarButtonPresetEnum::Select:
										case BarButtonPresetEnum::Draw:
										case BarButtonPresetEnum::Eraser:
										case BarButtonPresetEnum::Geometry:
										case BarButtonPresetEnum::Clean:
										case BarButtonPresetEnum::Recall:
										case BarButtonPresetEnum::Redo:
										case BarButtonPresetEnum::Pierce:
											Inkeys::UI::Bar::NotifyPptBusinessAction(); break;
										default: break;
										}
										lastClickedMainBarButton = temp;
										clickCompleted = true;
										UpdateRendering();

										break;
									}
								}
								else break;
							}
							SetBarButtonPressedVisual(*temp, false); UpdateRendering(false);
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
						const auto geometryAtPress = GetStateModeVersionedSnapshot();
						if (geometryAtPress.state.StateModeSelect != StateModeSelectEnum::IdtShape)
							continue;
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
							if (IsBarPointerCancelMessage(msg)) break;
							if (!shape->IsClick(msg.x, msg.y, barStyle.zoom)) break;
							if (!msg.lbutton)
							{
							if (StateModeTransitionRevision() != geometryAtPress.revision)
								break;
								if (button.closePanel)
									barState.geometryAttribute = false;
								else if (button.shapeMode.has_value())
								{
									SetShapeModeSelectIfRevision(
										button.shapeMode.value(), geometryAtPress.revision);
								}
								else SetPenWidthPresetIfRevision(button.thicknessPresetIndex,
									geometryAtPress.revision);
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
				PrepareBarMouseDragTracking();
				const auto gestureState = GetStateModeVersionedSnapshot();
				PenModeSelectEnum gesturePenMode =
					gestureState.state.Pen.ModeSelect;
				auto gestureRevision = gestureState.revision;
								auto range = GetBarThicknessSliderRange(
									gesturePenMode, barStyle.dpiZoom, gestureState.state);
				double initialWidth = GetPenWidthMetrics(gestureState.state).resolvedWidthDip;
								double finalWidth = initialWidth;
								double lastCandidateWidth = initialWidth;
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
										.thicknessFineDialCandidateActive
									&& thicknessFineDialModeRevision == gestureRevision)
								{
									double visualWidth = static_cast<double>(
										barState.drawAttributeBar
											.thicknessFineDialVisualWidth);
									if (isfinite(visualWidth))
									{
										initialWidth = clamp(
											visualWidth,
											static_cast<double>(range.min),
											static_cast<double>(range.max));
										finalWidth = initialWidth;
										lastCandidateWidth = initialWidth;
									}
								}

								POINT startScreenPoint = BarPointerScreenPoint(msg);
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
										trackTravelScreenX, barStyle.dpiZoom, gestureState.state));
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
									[&](double screenX) -> double
									{
										// screenX 表示目标圆点中心的屏幕横坐标。
										double rawWidth = range.min
											+ (screenX - trackStartScreenX)
												/ trackTravelScreenX * rangeSpan;
										double clampedWidth = clamp(
											rawWidth,
											static_cast<double>(range.min),
											static_cast<double>(range.max));
										return clampedWidth;
									};
								auto ProjectRelativePreviewWidth =
									[&](double screenX) -> double
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
										return clampedWidth;
									};
								auto ApplyCandidateWidth =
									[&](double targetWidth,
										bool candidateAllowed) -> bool
									{
										if (!PublishThicknessSliderCandidate(
											targetWidth, range, candidateAllowed)) return false;
										lastCandidateWidth = static_cast<double>(barState.drawAttributeBar
											.thicknessSliderCandidateWidth);
										finalWidth = lastCandidateWidth;
										candidateChanged = abs(finalWidth - initialWidth) > 0.000001;
										return true;
									};
							double fineDialAnchorScreenX = pressScreenX;
							double fineDialAnchorValue =
								static_cast<double>(initialWidth);
							double fineDialPressStartValue =
								static_cast<double>(initialWidth);
							if (viewModeAtPress == ThicknessViewMode::FineDial
								&& thicknessFineDialModeRevision == gestureRevision)
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
										double candidate =
											PublishThicknessFineDialCandidate();
										lastCandidateWidth = candidate;
										finalWidth = candidate;
										candidateChanged = abs(
											static_cast<double>(finalWidth - initialWidth))
											> 0.000001;
										AddThicknessFineDialSample(
											screenX, BarPointerSampleTick());
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
												finalWidth;
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
											BeginBarMouseDrag();
											barState.drawAttributeBar.thicknessViewMode =
												ThicknessViewMode::FineDial;
											barState.drawAttributeBar.thicknessSliderHover = false;
											barState.drawAttributeBar.thicknessSliderPinned = false;
											barState.drawAttributeBar.thicknessSliderDragging = false;
											barState.drawAttributeBar.thicknessSliderPressed = false;
											CloseThicknessOverflowTooltip();
							BeginThicknessFineDialDrag(startValue,
								screenX, unitTravelScreen, range, gestureRevision, candidateChanged);
											fineDialAnchorScreenX = screenX;
											fineDialAnchorValue = static_cast<double>(
												barState.drawAttributeBar
													.thicknessFineDialVisualWidth);
											lastCandidateWidth = static_cast<double>(barState.drawAttributeBar
												.thicknessSliderCandidateWidth);
											finalWidth = lastCandidateWidth;
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
											const bool hadCandidate = barState.drawAttributeBar.thicknessFineDialCandidateActive;
											const auto fineDialCommitRevision = CommitThicknessFineDialSelection();
											const auto committedRevision = hadCandidate ? fineDialCommitRevision : gestureRevision;
											const auto committedState = GetStateModeVersionedSnapshot();
											if (committedRevision == 0 || committedState.revision != committedRevision)
											{
												touchGestureCancelled = true;
												return;
											}
											// 仅接续本控件自己的提交版本，不接管期间到达的新工具/显示状态。
											gestureRevision = committedRevision;
											barPointerModeRevision = committedRevision;
											initialWidth = GetPenWidthMetrics(committedState.state).resolvedWidthDip;
											finalWidth = initialWidth;
											lastCandidateWidth = initialWidth;
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
											BeginBarMouseDrag();
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
							pressScreenX, unitTravelScreen, range, gestureRevision);
										fineDialAnchorScreenX = pressScreenX;
										fineDialAnchorValue = static_cast<double>(
											barState.drawAttributeBar
												.thicknessFineDialVisualWidth);
										lastCandidateWidth = static_cast<double>(barState.drawAttributeBar
												.thicknessSliderCandidateWidth);
										finalWidth = lastCandidateWidth;
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
										// 无输入时仍推进停留计时；模式快照不能冻结在按下时。
										stateMode = GetStateModeSnapshot();
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
											POINT cursorPoint = BarPointerScreenPoint(msg);
											if (barPointerSampleReady)
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

										if (IsBarPointerCancelMessage(msg))
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
											POINT screenPoint = BarPointerScreenPoint(msg);
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
												{
													gestureDragged = true;
													BeginBarMouseDrag();
												}
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
													BeginBarMouseDrag();
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
													BeginBarMouseDrag();
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
												BeginBarMouseDrag();
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
											BeginBarMouseDrag();
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
												POINT screenPoint = BarPointerScreenPoint(msg);
												ApplyFineDialScreenX(
													static_cast<double>(screenPoint.x));
											}
											else if (fineDialPopupReturnGesture)
											{
												POINT screenPoint = BarPointerScreenPoint(msg);
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
												POINT screenPoint = BarPointerScreenPoint(msg);
												double screenX = static_cast<double>(screenPoint.x);
												double screenY = static_cast<double>(screenPoint.y);
												lastTouchScreenX = screenX;
												lastTouchScreenY = screenY;
												if (!gestureDragged
													&& PopupDragThresholdExceeded(
														screenX, screenY))
												{
													gestureDragged = true;
													BeginBarMouseDrag();
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
												POINT screenPoint = BarPointerScreenPoint(msg);
												double moveDx = static_cast<double>(
													screenPoint.x) - pressScreenX;
												double moveDy = static_cast<double>(
													screenPoint.y) - pressScreenY;
												if (!gestureDragged
													&& moveDx * moveDx + moveDy * moveDy
														> touchDragThresholdSquared)
												{
													gestureDragged = true;
													BeginBarMouseDrag();
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
												POINT screenPoint = BarPointerScreenPoint(msg);
												if (!gestureDragged
													&& static_cast<double>(screenPoint.x)
														!= pressScreenX)
												{
													gestureDragged = true;
													BeginBarMouseDrag();
												}
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
											POINT cursorPoint = BarPointerScreenPoint(msg);
											if (barPointerSampleReady)
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
									bool gestureCompleted = msg.message == WM_LBUTTONUP
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
												SetPenWidthIfRevision(finalWidth, gestureRevision, true);
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
										if (canCommit)
											SetPenWidthIfRevision(finalWidth, gestureRevision, true);
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
		auto SelectPenTool = [&](PenToolSelectionEnum tool)
			{
				const bool expectedChange = PenToolSelectionWouldChange(tool);
				if (expectedChange)
				{
					// 先撤销 Fine Dial 候选，再发布新笔型，避免渲染线程吞掉量程过渡。
					ClosePenTypeMenu();
					if (barState.drawAttributeBar.thicknessViewMode == ThicknessViewMode::FineDial)
						CancelThicknessFineDialSelection();
				}
				const bool changed = ChangeStateModeToPenTool(tool);
				if (changed && !expectedChange)
				{
					// PPT 在预检与提交间接管时，仍收尾旧面板；最终工具由锁内事务决定。
					ClosePenTypeMenu();
					if (barState.drawAttributeBar.thicknessViewMode == ThicknessViewMode::FineDial)
						CancelThicknessFineDialSelection();
				}
				if (changed || expectedChange)
				{
					// 工具样式由统一状态刷新投影，避免在此前单独读取另一代状态。
					UpdateRendering();
				}
			};
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
						PrepareBarMouseDragTracking();
						BeginBarMouseDrag(); // 色块拖选按下即改色，结束仅退休连续会话。

						// 只有色块内的 Down 才启动拖选，穿过间隙后仍可切到新色块。
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
							{
								return BarInteractionStageResult::Shutdown;
							}
							if (IsBarPointerCancelMessage(msg)) break;
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
					bool laserThicknessPresetMode = IsLaserThicknessPresetMode(stateMode);
					bool thicknessPresetMode = laserThicknessPresetMode
						|| PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect, stateMode);
						for (const auto& button : thicknessButtons)
						{
							bool visible = thicknessPresetMode
								&& (!laserThicknessPresetMode || button.presetIndex >= 0);
							auto obj = shapeMap[button.shape];
							if (!visible || !obj
								|| !obj->IsClick(msg.x, msg.y, barStyle.zoom))
								continue;

							continueFlag = false;
							if (msg.message == WM_LBUTTONDOWN)
							{
								bool clickCompleted = false;
								const auto pressState = GetStateModeVersionedSnapshot();
								const auto toolRevisionAtPress = pressState.revision;
								const bool laserPresetAtPress =
									IsLaserThicknessPresetMode(pressState.state);
								if (pressState.state.StateModeSelect != StateModeSelectEnum::IdtPen ||
									(!laserPresetAtPress && !PenModeUsesThicknessPresets(
										pressState.state.Pen.ModeSelect, pressState.state)) ||
									(laserPresetAtPress && button.presetIndex < 0))
									continue;
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
									if (IsBarPointerCancelMessage(msg)) break;
									if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
									{
										if (!msg.lbutton)
										{
											if (StateModeTransitionRevision() != toolRevisionAtPress)
											{
												if (fineDialAtPress) CancelThicknessFineDialSelection();
												break;
											}
											if (button.presetIndex >= 0)
											{
												if (fineDialAtPress)
													CancelThicknessFineDialSelection();
												SetPenWidthPresetIfRevision(button.presetIndex, toolRevisionAtPress);
											}
											else
											{
												if (fineDialAtPress)
												{
													auto thicknessSliderRange = GetBarThicknessSliderRange(
														pressState.state.Pen.ModeSelect, barStyle.dpiZoom, pressState.state);
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
					&& !stateMode.laserActive
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
								if (IsBarPointerCancelMessage(msg)) break;
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
						barState.drawAttributeBar.laserPress = true;
						StopIndependentHover(hoveredIndependentButton, true, true);
						hoveredIndependentButton = IndependentHoverTargetEnum::None;
						UpdateRendering(false);
						bool clickCompleted = false;
						while (true)
						{
							if (!WaitForBarInteractionMessage(msg, EM_MOUSE, floating_window))
								return BarInteractionStageResult::Shutdown;
							if (IsBarPointerCancelMessage(msg)) break;
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom) && !msg.lbutton)
							{
								clickCompleted = true;
								ClosePenTypeMenu();
								CloseThicknessSlider(true);
								ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
								UpdateRendering();
								break;
							}
							if (!obj->IsClick(msg.x, msg.y, barStyle.zoom)) break;
						}
						barState.drawAttributeBar.laserPress = false;
						UpdateRendering(false);
						if (clickCompleted) SuppressHoverUntilPointerMove();
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
							if (IsBarPointerCancelMessage(msg)) break;
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
							if (!msg.lbutton)
							{
								SelectPenTool(PenToolSelectionEnum::HardPen);

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

				// 软笔：默认画笔，和硬笔共用粗细/颜色但保持独立按钮状态。
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_SoftPen];
					continueFlag && obj->IsClick(msg.x, msg.y, barStyle.zoom))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						barState.drawAttributeBar.softPenPress = true;
						StopIndependentHover(hoveredIndependentButton, true, true);
						hoveredIndependentButton = IndependentHoverTargetEnum::None;
						UpdateRendering(false);
						while (true)
						{
							if (!WaitForBarInteractionMessage(
								msg, EM_MOUSE, floating_window))
								return BarInteractionStageResult::Shutdown;
							if (IsBarPointerCancelMessage(msg)) break;
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
								if (!msg.lbutton)
								{
									SelectPenTool(PenToolSelectionEnum::SoftPen);
									break;
								}
							}
							else break;
						}
						barState.drawAttributeBar.softPenPress = false;
						UpdateRendering(false);
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
							if (IsBarPointerCancelMessage(msg)) break;
							if (obj->IsClick(msg.x, msg.y, barStyle.zoom))
							{
							if (!msg.lbutton)
							{
								SelectPenTool(PenToolSelectionEnum::Highlighter);

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
			if (stage == &BarInteractionSession::HandleMainButtonAndBarPointerStage
				&& result == BarInteractionStageResult::PassThrough && Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
			{
				Inkeys::UI::Bar::RejectAuthorizedFixtureSource(Inkeys::UI::Bar::Ui3FixtureSourceFailure::Action);
				return BarInteractionStageResult::Shutdown;
			}
			if (result != BarInteractionStageResult::PassThrough)
				return result;
		}
		return BarInteractionStageResult::PassThrough;
	}

public:
	void Run()
	{
		Inkeys::UI::Bar::EnterAuthorizedFixtureInteraction();
		struct FixtureInteractionExit { ~FixtureInteractionExit() { Inkeys::UI::Bar::LeaveAuthorizedFixtureInteraction(); } } fixtureExit;
		// 私有pure latch独立于Startup tracker；默认observer不存在时不工作。
		Inkeys::UI::Bar::NotifyFixtureInteractionReady();
		// Session 构造、mailbox 与交互状态全部可用后再报告 ready。
		(void)Inkeys::Startup::Report(
			Inkeys::Startup::Milestone::BarInteractionReady);
		while (!offSignal)
		{
			// 唯一业务安全点；下方各阶段与嵌套拖动均已退栈。
			if (barKeyboardShortcuts.Poll(true))
				this_thread::sleep_for(chrono::milliseconds(1));
			const auto pollResult = PollInteractionMessage();
			stateMode = GetStateModeSnapshot();
			if (pollResult == BarInteractionStageResult::Shutdown) break;
			if (pollResult == BarInteractionStageResult::Consumed) continue;
			if (Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
			{
				if (IsBarPointerCancelMessage(msg)) continue;
				// 所有辅助面板/工具写入前核同一 frozen Pen 与 closed aux 前提。
				if (!Inkeys::UI::Bar::PermitAuthorizedFixturePointerStages(
					barUISet.ReadFiniteSignature(GetStateModeVersionedSnapshot()))) break;
			}

			const auto keyboardResult = HandleKeyboardMessage();
			if (keyboardResult == BarInteractionStageResult::Shutdown) break;
			if (keyboardResult == BarInteractionStageResult::Consumed) continue;

			const auto dockIndicatorResult =
				HandleBottomDockIndicatorOcclusion();
			if (dockIndicatorResult == BarInteractionStageResult::Shutdown) break;
			if (dockIndicatorResult == BarInteractionStageResult::Consumed) continue;

			if (barUISet.eraserAttribute.Pointer(barUISet, msg, IsBarPointerCancelMessage(msg), IsBarTouchPointerMessage(msg)))
			{
				if (hoveredMainBarButton) { StopMainBarButtonHover(hoveredMainBarButton, true); hoveredMainBarButton = nullptr; }
				if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
				{ StopIndependentHover(hoveredIndependentButton, true); hoveredIndependentButton = IndependentHoverTargetEnum::None; }
				continue;
			}
			// 撤回属性与橡皮同级早处理；二者互斥，同时最多一个展开。
			if (barUISet.recallAttribute.Pointer(barUISet, msg, IsBarPointerCancelMessage(msg), IsBarTouchPointerMessage(msg)))
			{
				if (hoveredMainBarButton) { StopMainBarButtonHover(hoveredMainBarButton, true); hoveredMainBarButton = nullptr; }
				if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
				{ StopIndependentHover(hoveredIndependentButton, true); hoveredIndependentButton = IndependentHoverTargetEnum::None; }
				continue;
			}
			const auto hoverResult = HandleCommonHoverAndOcclusion();
			if (hoverResult == BarInteractionStageResult::Shutdown) break;
			if (hoverResult == BarInteractionStageResult::Consumed) continue;

			const auto pointerResult = DispatchPointerStages();
			if (pointerResult == BarInteractionStageResult::Shutdown) break;
			if (pointerResult == BarInteractionStageResult::Consumed) continue;
		}
		barKeyboardShortcuts.Stop();
		(void)FinishInteraction();
	}

private:
	bool WaitForBarInteractionMessage(ExMessage& message, BYTE filter, HWND hWnd,
		bool preserveTouchScreenCoordinates = false,
		BarTouchScreenSample* touchScreenSample = nullptr)
	{
		const bool received = ::WaitForBarInteractionMessage(message, filter, hWnd,
			preserveTouchScreenCoordinates, touchScreenSample);
		if (received) stateMode = GetStateModeSnapshot();
		return received;
	}

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
				IsBarPointerCancelMessage(message), true);
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
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeSoftPenHoverStage;
	IdtAtomic<BarButtonHoverStageEnum>& drawAttributeLaserHoverStage;
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
	StateModeClass stateMode = GetStateModeSnapshot();
	std::uint64_t thicknessFineDialModeRevision = 0;

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
	double thicknessFineDialOriginDip = 0.0;
	double thicknessFineDialVisualValue = 0.0;
	double thicknessFineDialVelocity = 0.0;
	double thicknessFineDialResidualVelocity = 0.0;
	double thicknessFineDialSettleTarget = 0.0;
	double thicknessFineDialUnitTravelScreen = 1.0;
	double thicknessFineDialRangeMin = 0.0;
	double thicknessFineDialRangeMax = 0.0;
	ULONGLONG thicknessFineDialGrabTick = 0;
	chrono::steady_clock::time_point thicknessFineDialLastPhysicsTime =
		chrono::steady_clock::now();
	bool thicknessFineDialPhysicsClockNeedsReset = true;
	bool thicknessFineDialCommitIssued = false;
	bool thicknessFineDialDirty = false;
	// FineDial 隐藏 Slider 固定态，Popup 返回时恢复进入前的生命周期。
	bool thicknessFineDialReturnPinned = false;
};
}

namespace Inkeys::UI::Bar
{
	int RunThicknessFineDialProductionTest() noexcept
	{
		return BarInteractionSession::RunThicknessFineDialProductionTest();
	}
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
	// 光效休眠只关闭观察状态，不能注销拖动与 PageControl 共用的鼠标输入。
	const bool available = enabled && (Pointer::RawMouseAvailable()
		|| Pointer::AttachRawMouse(hWnd));
	{
		lock_guard lock(borderCursorLightMutex);
		borderCursorRawInputRegistered = available;
		borderCursorInputAvailable = available;
	}
	return !enabled || available;
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
		const D2D1_POINT_2F nextScreenPoint = D2D1::Point2F(
			static_cast<FLOAT>(screenPoint.x), static_cast<FLOAT>(screenPoint.y));
		// Inside 始终发布真实光标点，不能依赖首帧可见区域缓存已经完成。
		bool cursorChanged = !borderCursorLightReady
			|| nextPoint.x != borderCursorLightPoint.x
			|| nextPoint.y != borderCursorLightPoint.y
			|| nextScreenPoint.x != borderCursorLightScreenPoint.x
			|| nextScreenPoint.y != borderCursorLightScreenPoint.y;
		borderCursorLightReady = true;
		if (cursorChanged)
		{
			borderCursorLightPoint = nextPoint;
			borderCursorLightScreenPoint = nextScreenPoint;
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
	if (IsBorderCursorAcceptingWindow(
		hWnd, WindowFromPoint(screenPoint)))
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
			const D2D1_POINT_2F nextScreenPoint = D2D1::Point2F(
				static_cast<FLOAT>(screenPoint.x), static_cast<FLOAT>(screenPoint.y));
			// 跨出 240px 时发布最后一个位置，让 240px 径向渐变自然落到 0。
			cursorChanged = !borderCursorLightReady
				|| nextPoint.x != borderCursorLightPoint.x
				|| nextPoint.y != borderCursorLightPoint.y
				|| nextScreenPoint.x != borderCursorLightScreenPoint.x
				|| nextScreenPoint.y != borderCursorLightScreenPoint.y;
			if (cursorChanged)
			{
				borderCursorLightPoint = nextPoint;
				borderCursorLightScreenPoint = nextScreenPoint;
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

	// 落笔后 Redo 不可用：先清锁存，再收起浮窗，避免收起路径又收成 2×1。
	ClearRecallForDrawing();
	// UI3 落笔时固定收起次级面板；主栏本身保持用户当前的展开状态。
	CollapseAuxiliaryPanels(true);
	UpdateRendering(false);

	// 即使光标仍在 Bar/PageControl 内也先休眠，并等待真实离开后再允许激活。
	SuspendBorderCursorTracking(hWnd, true);
}

void BarUISetClass::SuspendBorderCursorTracking(HWND hWnd, bool waitForMouseLeave)
{
	if (hWnd) KillTimer(hWnd, BarBorderCursorGraceTimerId);
	bool blockActivationUntilLeave = false;
	if (waitForMouseLeave && hWnd)
	{
		POINT screenPoint{};
		const HWND receivingWindow = GetCursorPos(&screenPoint)
			? WindowFromPoint(screenPoint) : nullptr;
		if (IsBorderCursorAcceptingWindow(hWnd, receivingWindow))
		{
			TRACKMOUSEEVENT trackMouseEvent{
				sizeof(TRACKMOUSEEVENT), TME_LEAVE, receivingWindow, 0 };
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
	array<RECT, 10> nextRegions{};
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
					bounds, bottomDockSnapshot.horizontalMapping,
					bottomDockSnapshot.mapping, frameZoom)
				: Inkeys::UI::Bar::TranslateBarBottomDockRigidRect(bounds,
					bottomDockSnapshot.horizontalMapping
						.rigidOverlayTranslationXDip,
					bottomDockSnapshot.rigidTranslationDip, frameZoom);
		};
	auto AddSuperellipse = [&](const shared_ptr<BarUiSuperellipseClass>& superellipse)
		{
			if (!superellipse || !superellipse->enable.val
				|| superellipse->pct.val <= 0.000001 || nextCount >= nextRegions.size())
				return;
			RECT bounds = BarRenderingAttribute::GetWeigetRect(
				*superellipse, frameZoom);
			nextRegions[nextCount++] = Inkeys::UI::Bar::TransformBarBottomDockGripRect(
				bounds, bottomDockSnapshot.horizontalMapping,
				bottomDockSnapshot.mapping, frameZoom);
		};

	AddSuperellipse(superellipseMap[BarUISetSuperellipseEnum::MainButton]);
	AddShape(shapeMap[BarUISetShapeEnum::MainBar], true);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar], false);
	AddShape(shapeMap[BarUISetShapeEnum::GeometryAttributeBar], false);
	for (const RECT& bounds : eraserAttribute.PresentedRegions())
		if (bounds.right > bounds.left && bounds.bottom > bounds.top && nextCount < nextRegions.size())
			nextRegions[nextCount++] = Inkeys::UI::Bar::TranslateBarBottomDockRigidRect(bounds,
				bottomDockSnapshot.horizontalMapping.rigidOverlayTranslationXDip,
				bottomDockSnapshot.rigidTranslationDip, frameZoom);
	for (const RECT& bounds : recallAttribute.PresentedRegions())
		if (bounds.right > bounds.left && bounds.bottom > bounds.top && nextCount < nextRegions.size())
			nextRegions[nextCount++] = Inkeys::UI::Bar::TranslateBarBottomDockRigidRect(bounds,
				bottomDockSnapshot.horizontalMapping.rigidOverlayTranslationXDip,
				bottomDockSnapshot.rigidTranslationDip, frameZoom);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel], false);
	AddShape(shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble], false);
	if (bottomDockSnapshot.indicatorVisible
		&& bottomDockSnapshot.indicatorBounds.right
			> bottomDockSnapshot.indicatorBounds.left
		&& bottomDockSnapshot.indicatorBounds.bottom
			> bottomDockSnapshot.indicatorBounds.top
		&& nextCount < nextRegions.size())
	{
		// 指示器只使用成功呈现边界，第三光源邻域不能领先于屏幕像素。
		nextRegions[nextCount++] = bottomDockSnapshot.indicatorBounds;
	}

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
	array<RECT, 10> visibleRegions{};
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
	auto IsNear = [&](const RECT& region)
		{
			if (region.right <= region.left || region.bottom <= region.top)
				return false;
		double dx = 0.0;
		double dy = 0.0;
		if (screenPoint.x < region.left) dx = static_cast<double>(region.left - screenPoint.x);
		else if (screenPoint.x > region.right) dx = static_cast<double>(screenPoint.x - region.right);
		if (screenPoint.y < region.top) dy = static_cast<double>(region.top - screenPoint.y);
		else if (screenPoint.y > region.bottom) dy = static_cast<double>(screenPoint.y - region.bottom);
		double distanceSquared = dx * dx + dy * dy;
		return distanceSquared <= distanceLimitSquared;
		};
	for (size_t i = 0; i < visibleRegionCount; i++)
		if (IsNear(visibleRegions[i])) return true;

	const auto surfaceRegions = SnapshotBorderCursorSurfaceRegions();
	auto& service = Inkeys::Window::GetService();
	for (std::size_t index = 0; index < surfaceRegions.bounds.size(); ++index)
	{
		if (!surfaceRegions.visible[index]
			|| !IsNear(surfaceRegions.bounds[index])) continue;
		const HWND surface = service.Handle(BorderCursorSurfaceRoles[index]);
		if (surface && IsWindowVisible(surface)) return true;
	}
	return false;
}

// 拖动交互
BarSeekResult BarUISetClass::Seek(const ExMessage& msg)
{
	using namespace Inkeys::UI::Bar;
	BarSeekResult result;
	if (IsBottomDockIndicatorPresentedAt(msg.x, msg.y))
	{
		result.allowClick = false;
		return result;
	}
	if (!msg.lbutton || IsBarPointerCancelMessage(msg))
	{
		result.allowClick = false;
		return result;
	}

	auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
	if (!mainButton) return result;
	PrepareBarMouseDragTracking();

	const BarPendingDisplaySnapshot initialDisplaySnapshot =
		PendingDisplaySnapshot();
	auto ResolveInteractionZoom = [&](const BarPendingDisplaySnapshot& snapshot)
		{
			return ResolveBarBottomDockInteractionZoom(snapshot.dpi,
				static_cast<double>(barStyle.configZoom), snapshot.uiBaseScale);
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
	const POINT initialPresentedTranslation =
		initialPresentedSnapshot.directTranslation;
	const BarBottomDockMode initialMode = initialPresentedSnapshot.mode;
	const BarBottomDockCenterMode initialCenterMode =
		initialPresentedSnapshot.centerMode;
	bool whiteboardDockLocked = WhiteboardDockLockActive();
	const BarBottomDockPhase initialPhase = initialPresentedSnapshot.phase;
	const BarBottomDockPhase initialCenterPhase =
		initialPresentedSnapshot.centerPhase;
	const bool initialRecoveryActive =
		initialPresentedSnapshot.recoveryActive;
	const double initialPresentedElasticDip =
		initialPresentedSnapshot.elasticOffsetDip;
	const double initialPresentedCenterElasticDip =
		initialPresentedSnapshot.centerElasticOffsetDip;
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
			- initialPresentedElasticDip) > 0.000001
		|| bottomDockCenterMode.load(memory_order_acquire) != initialCenterMode
		|| bottomDockCenterPhase.load(memory_order_acquire) != initialCenterPhase
		|| abs(bottomDockCenterElasticOffsetDip.load(memory_order_acquire)
			- initialPresentedCenterElasticDip) > 0.000001;
	unsigned long long gestureRebaseSerial = 0;
	if (presentedStateRebaseRequired)
	{
		// 上一手势可能尚未提交 ULW；新手势先撤回到同一份已呈现 tuple。
		BeginBottomDockTransition();
		bottomDockMode.store(initialMode, memory_order_relaxed);
		bottomDockPhase.store(initialPhase, memory_order_relaxed);
		bottomDockRecoveryActive.store(
			initialRecoveryActive, memory_order_relaxed);
		bottomDockElasticOffsetDip.store(
			initialPresentedElasticDip, memory_order_relaxed);
		bottomDockCenterMode.store(initialCenterMode, memory_order_relaxed);
		bottomDockCenterPhase.store(initialCenterPhase, memory_order_relaxed);
		bottomDockCenterElasticOffsetDip.store(
			initialPresentedCenterElasticDip, memory_order_relaxed);
		directWindowDragTranslationX.store(
			initialPresentedTranslation.x, memory_order_relaxed);
		directWindowDragTranslationY.store(
			initialPresentedTranslation.y, memory_order_relaxed);
		gestureRebaseSerial = FinishBottomDockTransition(true);
	}
	double baseMainCenterScreenX =
		initialPresentedSnapshot.rawMainCenterScreenX
		- initialPresentedTranslation.x
		- initialPresentedSnapshot.horizontalMapping.rigidGripTranslationXDip * presentedZoom;
	double baseMainCenterScreenY =
		initialPresentedSnapshot.mainCenterScreenY
		- initialPresentedTranslation.y;
	double bodyToMainCenterScreenX =
		initialPresentedSnapshot.rawBodyCenterScreenX
			- initialPresentedSnapshot.rawMainCenterScreenX;
	LONG appliedDeltaX = directWindowDragTranslationX.load(memory_order_acquire);
	LONG appliedDeltaY = directWindowDragTranslationY.load(memory_order_acquire);
	bool directMoveFailed = false;

		auto ResolveDockInsetDip = []() noexcept
			{
				return Inkeys::UI::Bar::SceneBottomDockInsetDip();
			};
		auto ResolveDockDpiScale = [](UINT dpi) noexcept
			{
				return clamp(
					static_cast<double>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) /
					static_cast<double>(USER_DEFAULT_SCREEN_DPI), 0.5, 4.0);
			};
		BarBottomDockEnvironment environment{
			initialPresentedSnapshot.monitorBounds,
			initialPresentedSnapshot.workArea,
			interactionZoom,
			ResolveDockInsetDip(),
			ResolveDockDpiScale(initialPresentedSnapshot.dpi),
			initialPresentedSnapshot.desktopDockLine };
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

	const POINT startPointer = BarPointerScreenPoint(msg);
	POINT previousPointer = startPointer;
	const double actualMainCenterScreenX =
		initialPresentedSnapshot.mainCenterScreenX;
	const double actualMainCenterScreenY =
		initialPresentedSnapshot.mainCenterScreenY;
	double grabOffsetScreenX = startPointer.x - actualMainCenterScreenX;
	double grabOffsetScreenY = startPointer.y
		- (actualMainCenterScreenY
			+ initialPresentedSnapshot.rigidTranslationDip * presentedZoom);
	const double initialFloatingVisibleBottomScreenY =
		startPointer.y - grabOffsetScreenY + VisibleHalfHeightScreen();
		// 任务栏动画中按下时，抓取已显示的中间位置，不能按终点制造脱离位移。
		const double initialDockCenterScreenY = initialMode == BarBottomDockMode::BottomDocked
			&& environment.insetDip <= 0.0
			? actualMainCenterScreenY : ResolveBarBottomDockCenterScreenY(
			ResolveBarBottomDockLine(
				environment.monitorBounds, environment.workArea,
				environment.insetDip, environment.dpiScale, environment.desktopDockLine),
			bodyHeightDip, strokeWidthDip, interactionZoom);
	BarBottomDockDragTracker dockTracker;
	dockTracker.Begin(initialMode, startPointer.y,
		initialFloatingVisibleBottomScreenY,
		initialDockCenterScreenY + grabOffsetScreenY, environment);
	BarBottomDockCenterDragTracker centerTracker;
	centerTracker.Begin(initialCenterMode,
		initialPresentedSnapshot.rawBodyCenterScreenX,
		initialMode == BarBottomDockMode::BottomDocked,
		!barState.fold, environment);
	bool dockIndicatorGestureActive = false;
	BeginBottomDockTransition();
	bottomDockDragRigidGripScreenX.store(
		startPointer.x - grabOffsetScreenX, memory_order_relaxed);
	bottomDockDragRigidGripScreenY.store(
		startPointer.y - grabOffsetScreenY, memory_order_relaxed);
	bottomDockIndicatorGestureEligible.store(
		false,
		memory_order_relaxed);
	bottomDockDragActive.store(true, memory_order_relaxed);
	bottomDockPhase.store(initialMode == BarBottomDockMode::BottomDocked
		? BarBottomDockPhase::Dragging : initialPhase, memory_order_relaxed);
	bottomDockCenterPhase.store(initialCenterMode
		== BarBottomDockCenterMode::Centered
		? BarBottomDockPhase::Dragging : initialCenterPhase,
		memory_order_relaxed);
	if (initialMode == BarBottomDockMode::BottomDocked
		|| presentedStateRebaseRequired)
		bottomDockElasticOffsetDip.store(
			initialPresentedElasticDip, memory_order_relaxed);
	(void)FinishBottomDockTransition();
	UpdateRendering(false);

	double maximumElasticTravelScreen = 0.0;
	double lastPublishedElasticDip = initialPresentedElasticDip;
	double lastPublishedCenterElasticDip = initialPresentedCenterElasticDip;
	bool downwardDetachSeen = false;
	bool downwardDetachBlockedAtRelease = false;
	bool gestureCancelled = false;
	DragCollapseGate dragCollapseGate;
	unsigned long long awaitedTransitionSerial = gestureRebaseSerial;
	auto PublishPresentationBarrier = [&](POINT pointer)
		{
			// 缩放或显示参数改变时，先让 ULW 提交新尺寸，再允许直移旧位图。
			BeginBottomDockTransition();
			// 显示屏障也成对发布抓手坐标，不能让首帧混用旧 X 和新 Y。
			bottomDockDragRigidGripScreenX.store(
				pointer.x - grabOffsetScreenX, memory_order_relaxed);
			bottomDockDragRigidGripScreenY.store(
				pointer.y - grabOffsetScreenY, memory_order_relaxed);
			const auto barrierSerial = FinishBottomDockTransition(true);
			awaitedTransitionSerial = barrierSerial;
			UpdateRendering(false);
		};
	struct DockPublication
	{
		bool visualChanged = false;
		unsigned long long transitionSerial = 0;
	};
	auto PublishDockUpdate = [&](const BarBottomDockDragUpdate& update,
		const BarBottomDockCenterDragUpdate& centerUpdate, bool foldedDuringDrag)
		{
			DockPublication publication;
			publication.visualChanged = foldedDuringDrag || update.modeChanged || centerUpdate.modeChanged
				|| abs(update.elasticOffsetDip - lastPublishedElasticDip) > 0.000001
				|| abs(centerUpdate.elasticOffsetDip
					- lastPublishedCenterElasticDip) > 0.000001;
			bottomDockMode.store(update.mode, memory_order_release);
			bottomDockPhase.store(update.phase, memory_order_release);
			bottomDockElasticOffsetDip.store(
				update.elasticOffsetDip, memory_order_release);
			bottomDockCenterMode.store(centerUpdate.mode, memory_order_release);
			bottomDockCenterPhase.store(centerUpdate.phase, memory_order_release);
			bottomDockCenterElasticOffsetDip.store(
				centerUpdate.elasticOffsetDip, memory_order_release);
			const auto indicatorEligibility = Inkeys::UI::Bar::
				ResolveBarBottomDockIndicatorGestureEligibility(
					dockIndicatorGestureActive,
					update.captured
						&& update.mode == BarBottomDockMode::BottomDocked,
					centerUpdate.modeChanged,
					update.mode == BarBottomDockMode::BottomDocked,
					!barState.fold);
			dockIndicatorGestureActive = indicatorEligibility.gestureActive;
			const bool nextIndicatorGestureEligible = indicatorEligibility.eligible;
			publication.visualChanged = publication.visualChanged
				|| bottomDockIndicatorGestureEligible.load(memory_order_relaxed)
					!= nextIndicatorGestureEligible;
			// 竖向进入底栏或水平模式切换后锁存；仅离开底栏、折叠或结束手势清除。
			bottomDockIndicatorGestureEligible.store(
				nextIndicatorGestureEligible, memory_order_release);
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
			if (update.modeChanged || centerUpdate.modeChanged)
			{
				result.captured = result.captured || update.captured;
				result.detached = result.detached || update.detached;
				result.modeChanged = true;
				result.allowClick = false;
			}
			// 偶数 serial 发布在两轴模式、阶段、形变量和位移之后。
			publication.transitionSerial = FinishBottomDockTransition(
				update.modeChanged || centerUpdate.modeChanged || foldedDuringDrag);
			maximumElasticTravelScreen = max(maximumElasticTravelScreen,
				abs(update.elasticOffsetDip - initialPresentedElasticDip)
					* interactionZoom);
			maximumElasticTravelScreen = max(maximumElasticTravelScreen,
				abs(centerUpdate.elasticOffsetDip
					- initialPresentedCenterElasticDip) * interactionZoom);
			lastPublishedElasticDip = update.elasticOffsetDip;
			lastPublishedCenterElasticDip = centerUpdate.elasticOffsetDip;
			return publication;
		};

	double diagnosticDesiredCenterX = actualMainCenterScreenX;
	double diagnosticDesiredCenterY = actualMainCenterScreenY;
	ULONGLONG diagnosticLastTick = 0;
	bool diagnosticEnvironmentChanged = false;
	barPointerDragDiagnostics = {};
	barPointerDragDiagnostics.active = true;
	RecordBarDragPointerArrival(false, barPointerLatest);
	RecordBarDragPointerDecision(false, true);
	auto LogDragDiagnostics = [&](const char* event, bool force = false)
		{
			const auto now = GetTickCount64();
			if (!force && now - diagnosticLastTick < 500) return;
			diagnosticLastTick = now;
			if (!IDTLogger) return;
			const auto presented = BottomDockPresentedSnapshot();
			const auto pending = PendingDisplaySnapshot();
			POINT cursor{};
			const bool cursorValid = GetCursorPos(&cursor) != FALSE;
			GUITHREADINFO ownerInfo{ sizeof(GUITHREADINFO) };
			const bool ownerInfoValid = GetGUIThreadInfo(
				GetWindowThreadProcessId(floating_window, nullptr), &ownerInfo) != FALSE;
			const auto& trace = barPointerDragDiagnostics;
			// 光标只用于对比样本/呈现，不生成移动，也不据此推断按键状态。
			IDTLogger->info(
				"[UiPointerDrag] event={} tick={} session={} source={} active={} dragging={} "
				"native={}/{} last=({},{},t{},k{},g{}) raw={}/{} last=({},{},t{},k{},g{},c{}) "
				"armed={} bound={} disabled={} rawActivity={} accepted=({},{},t{},k{}) "
				"cursor=({},{},valid{}) grab=({},{}) desired=({},{}) "
				"presented=({},{},rawX{},rigidY{}) applied=({},{}) presentedTranslation=({},{}) "
				"display={}/{} zoom={}/{} "
				"monitor=({},{},{},{}) transition={}/{} envChanged={} "
				"foreground={} capture={} ownerInfo={} cancelled={} moveFailed={}",
				event, now, barPointerLatest.generation, static_cast<int>(barPointerLatest.source),
				BarPointerSessionActive(), barMouseGesture.Dragging(),
				trace.nativeConsumed, trace.nativeAccepted,
				trace.nativeLatest.screen.x, trace.nativeLatest.screen.y,
				trace.nativeLatest.messageTime, static_cast<int>(trace.nativeLatest.kind),
				trace.nativeLatest.generation, trace.rawConsumed, trace.rawAccepted,
				trace.rawLatest.screen.x, trace.rawLatest.screen.y,
				trace.rawLatest.messageTime, static_cast<int>(trace.rawLatest.kind),
				trace.rawLatest.generation, static_cast<int>(trace.rawLatest.cancelReason),
				barMouseArmed, trace.rawBound, trace.rawDisabled, trace.rawActivityTime,
				barPointerLatest.screen.x, barPointerLatest.screen.y,
				barPointerLatest.messageTime, static_cast<int>(barPointerLatest.kind),
				cursor.x, cursor.y, cursorValid, grabOffsetScreenX, grabOffsetScreenY,
				diagnosticDesiredCenterX, diagnosticDesiredCenterY,
				presented.mainCenterScreenX, presented.mainCenterScreenY,
				presented.rawMainCenterScreenX, presented.rigidTranslationDip * presented.zoom,
				appliedDeltaX, appliedDeltaY, presented.directTranslation.x, presented.directTranslation.y,
				presented.displaySerial, pending.serial, presented.zoom, ResolveInteractionZoom(pending),
				presented.monitorBounds.left, presented.monitorBounds.top,
				presented.monitorBounds.right, presented.monitorBounds.bottom,
				presented.transitionSerial, awaitedTransitionSerial, diagnosticEnvironmentChanged,
				reinterpret_cast<std::uintptr_t>(GetForegroundWindow()),
				reinterpret_cast<std::uintptr_t>(ownerInfo.hwndCapture), ownerInfoValid,
				gestureCancelled, directMoveFailed);
			diagnosticEnvironmentChanged = false;
		};
	LogDragDiagnostics("begin", true);

	auto ApplyAbsolutePointer = [&](POINT pointer)
		{
			const double stepX = static_cast<double>(pointer.x - previousPointer.x);
			const double stepY = static_cast<double>(pointer.y - previousPointer.y);
			const double stepLength = hypot(stepX, stepY);
			result.rawPathLength += stepLength;
			result.moved = result.moved || stepLength > 0.0;
			previousPointer = pointer;
			auto RebasePointerForPresentedEnvironment = [&](const auto& presented)
				{
					// 显示环境只更新布局基准；受限或滞后的呈现位置不能重设抓住的逻辑点。
					baseMainCenterScreenX = presented.rawMainCenterScreenX
						- presented.directTranslation.x
						- presented.horizontalMapping.rigidGripTranslationXDip * presented.zoom;
					baseMainCenterScreenY = presented.mainCenterScreenY
						- presented.directTranslation.y;
					bodyToMainCenterScreenX = presented.rawBodyCenterScreenX
						- presented.rawMainCenterScreenX;
					const double floatingBottom = pointer.y - grabOffsetScreenY
						+ VisibleHalfHeightScreen();
						// 已吸附手势保留成功呈现的基线，新的任务栏终点由松手后的动画消费。
						const double dockCenter = dockTracker.Mode() == BarBottomDockMode::BottomDocked
							&& environment.insetDip <= 0.0
							? presented.mainCenterScreenY : ResolveBarBottomDockCenterScreenY(
							ResolveBarBottomDockLine(
								environment.monitorBounds, environment.workArea,
								environment.insetDip, environment.dpiScale, environment.desktopDockLine),
							bodyHeightDip, strokeWidthDip, interactionZoom);
					dockTracker.RebaseDockGrip(
						dockCenter + grabOffsetScreenY, floatingBottom);
					centerTracker.Rebase(
						presented.rawBodyCenterScreenX,
						presented.monitorBounds);
					appliedDeltaX = directWindowDragTranslationX.load(
						memory_order_acquire);
					appliedDeltaY = directWindowDragTranslationY.load(
						memory_order_acquire);
				};
			auto AdoptPresentedEnvironment = [&](const auto& presented)
				{
					if (abs(presented.zoom - interactionZoom) > 0.000001)
					{
						grabOffsetScreenX = ScaleBarDragGrabOffsetForZoom(
							grabOffsetScreenX, interactionZoom, presented.zoom);
						grabOffsetScreenY = ScaleBarDragGrabOffsetForZoom(
							grabOffsetScreenY, interactionZoom, presented.zoom);
					}
					interactionZoom = presented.zoom;
					maximumInteractionZoom = max(
						maximumInteractionZoom, interactionZoom);
						environment = BarBottomDockEnvironment{
							presented.monitorBounds,
							presented.workArea,
							interactionZoom,
							ResolveDockInsetDip(),
							ResolveDockDpiScale(presented.dpi),
							presented.desktopDockLine };
					observedDisplaySerial = presented.displaySerial;
					requestedDisplaySerial = presented.displaySerial;
					requestedDisplayZoom = interactionZoom;
				};

			const auto presented = BottomDockPresentedSnapshot();
			const bool presentedEnvironmentChanged =
				presented.displaySerial != observedDisplaySerial
				|| abs(presented.zoom - interactionZoom) > 0.000001;
			if (presentedEnvironmentChanged)
			{
				// 真实上屏的环境立即接管阈值；形态 barrier 可继续等待自己的 serial。
				AdoptPresentedEnvironment(presented);
				RebasePointerForPresentedEnvironment(presented);
				displayEnvironmentAwaited = false;
				diagnosticEnvironmentChanged = true;
			}
			if (awaitedTransitionSerial != 0
				&& presented.transitionSerial >= awaitedTransitionSerial)
			{
				if (displayEnvironmentAwaited)
					AdoptPresentedEnvironment(presented);
				// 形态 barrier 只确认 HWND 位移已上屏，禁止用形变结果重设抓取基准。
				appliedDeltaX = directWindowDragTranslationX.load(
					memory_order_acquire);
				appliedDeltaY = directWindowDragTranslationY.load(
					memory_order_acquire);
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
				PublishPresentationBarrier(pointer);
			}

			const double floatingVisibleBottomScreenY =
				pointer.y - grabOffsetScreenY + VisibleHalfHeightScreen();
			const BarBottomDockDragUpdate dockUpdate = dockTracker.Update(
				pointer.y, floatingVisibleBottomScreenY, environment);
			const bool realDrag = !ShouldAllowBarBottomDockClick(false, false,
				result.modeChanged || dockUpdate.modeChanged, result.rawPathLength,
				maximumElasticTravelScreen, maximumInteractionZoom);
			const bool foldedDuringDrag = !barState.fold && dragCollapseGate.Take(
				Inkeys::config.UI.Bar.AutoCollapse.Dragging, dockUpdate.mode, realDrag)
				&& FoldBarAutomatically(true);
			const double rawMainCenterScreenX = pointer.x - grabOffsetScreenX;
			const double rawBodyCenterScreenX = rawMainCenterScreenX
				+ bodyToMainCenterScreenX;
			const BarBottomDockCenterDragUpdate centerUpdate = centerTracker.Update(
				rawBodyCenterScreenX,
				dockUpdate.mode == BarBottomDockMode::BottomDocked,
				!barState.fold, environment);
			if (whiteboardDockLocked && dockUpdate.modeChanged)
			{
				// 手动离开或重新捕获底栏后即恢复普通拖动，不再保留专用横向锁。
				ClearWhiteboardDockLock();
				whiteboardDockLocked = false;
			}

			double desiredMainCenterScreenX = whiteboardDockLocked
				? actualMainCenterScreenX : pointer.x - grabOffsetScreenX;
			if (centerUpdate.mode == BarBottomDockCenterMode::Centered)
				desiredMainCenterScreenX = centerUpdate.constrainedCenterScreenX
					- bodyToMainCenterScreenX;
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
							environment.workArea, environment.insetDip,
							environment.dpiScale, environment.desktopDockLine), bodyHeightDip,
						strokeWidthDip, interactionZoom);
				downwardDetachBlockedAtRelease =
					ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
						downwardDetachSeen, rawMainCenterScreenY,
						maximumY, dockCenterScreenY);
			}
			if (dockUpdate.mode == BarBottomDockMode::BottomDocked)
				downwardDetachBlockedAtRelease = false;

			diagnosticDesiredCenterX = desiredMainCenterScreenX;
			diagnosticDesiredCenterY = desiredMainCenterScreenY;
			const LONG pixelDeltaX = static_cast<LONG>(lround(
				desiredMainCenterScreenX - baseMainCenterScreenX));
			const LONG pixelDeltaY = static_cast<LONG>(lround(
				desiredMainCenterScreenY - baseMainCenterScreenY));
			const bool translationChanged = pixelDeltaX != appliedDeltaX
				|| pixelDeltaY != appliedDeltaY;
			const bool anyModeChanged = dockUpdate.modeChanged
				|| centerUpdate.modeChanged;
			// 每个采样都用同一个 seqlock tuple 发布抓手、两轴映射和窗口位移。
			BeginBottomDockTransition();
			bottomDockDragRigidGripScreenX.store(
				pointer.x - grabOffsetScreenX, memory_order_relaxed);
			bottomDockDragRigidGripScreenY.store(
				pointer.y - grabOffsetScreenY, memory_order_relaxed);
			if (translationChanged)
			{
				directWindowDragTranslationX.store(
					pixelDeltaX, memory_order_release);
				directWindowDragTranslationY.store(
					pixelDeltaY, memory_order_release);
			}
			const DockPublication publication = PublishDockUpdate(
				dockUpdate, centerUpdate, foldedDuringDrag);
			if (anyModeChanged || foldedDuringDrag)
			{
				// 捕获/脱离必须先由 ULW 同帧提交新位图和新位置，
				// 在此之前禁止当前或后续采样用 SetWindowPos 移动上一帧位图。
				awaitedTransitionSerial = publication.transitionSerial;
			}
			bool requestRendering = publication.visualChanged;
			if (translationChanged)
			{
				const bool deferDirectMove = awaitedTransitionSerial != 0;
				if (!deferDirectMove)
				{
					unique_lock lock(directWindowDragMutex, try_to_lock);
					if (lock.owns_lock() && committedWindowScreenBoundsReady)
					{
						// ULW 后续阶段失败时缓存不可直移，交给完整呈现重试接管。
						const RECT currentWindowRect = committedWindowScreenBounds;
						const POINT desiredTranslation{ pixelDeltaX, pixelDeltaY };
						const POINT presentedTranslation{
							directWindowPresentedTranslationX.load(memory_order_acquire),
							directWindowPresentedTranslationY.load(memory_order_acquire) };
						const POINT moveDelta = ResolveBarDirectWindowMoveDelta(
							desiredTranslation, presentedTranslation);
						const RECT targetWindow = TranslateBarWindowRect(currentWindowRect, moveDelta);
						const auto display = PendingDisplaySnapshot();
						const bool protect = display.protectBottomEdge && ResolveDockInsetDip() <= 0.0;
						if (!Inkeys::UI::Bar::IsBarWindowAboveTaskbarEdge(targetWindow, display.bounds.bottom, protect))
						{
							// 旧位图不能越过底行；保留输入位移，让下一次 ULW 同时更新裁剪和位置。
							appliedDeltaX = pixelDeltaX;
							appliedDeltaY = pixelDeltaY;
							lock.unlock();
							UpdateRendering(false);
							return true;
						}
						if ((moveDelta.x != 0 || moveDelta.y != 0)
							&& !SetWindowPos(floating_window, nullptr,
								currentWindowRect.left + moveDelta.x,
								currentWindowRect.top + moveDelta.y, 0, 0,
								SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE))
						{
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

	ExMessage pointerMessage = msg;
	while (!offSignal && !directMoveFailed)
	{
		// 无输入时也输出限频摘要，才能区分流中断与已到达样本被仲裁拒绝。
		LogDragDiagnostics("tracking");
		if (!TryReadBarPointerMessage(pointerMessage, EM_MOUSE, floating_window, true, nullptr))
		{
			this_thread::sleep_for(chrono::milliseconds(1));
			continue;
		}
		if (IsBarPointerCancelMessage(pointerMessage))
		{
			gestureCancelled = true;
			break;
		}
		(void)ApplyAbsolutePointer(BarPointerScreenPoint(pointerMessage));
		if (!ShouldAllowBarBottomDockClick(false, false, result.modeChanged,
			result.rawPathLength, maximumElasticTravelScreen, maximumInteractionZoom))
			BeginBarMouseDrag(); // 沿用原点击阈值；阈值内 Raw Up 不能触发折叠。
		if (pointerMessage.message == WM_LBUTTONUP) break;
	}

	if (offSignal) gestureCancelled = true;
	LogDragDiagnostics("end", true);
	barPointerDragDiagnostics.active = false;

	if (directMoveFailed)
	{
		// 直接移动失败时以最后一次真实上屏 tuple 为准，不能回到手势开始时的旧快照。
		const auto rollbackSnapshot = BottomDockPresentedSnapshot();
		BeginBottomDockTransition();
		bottomDockMode.store(rollbackSnapshot.mode, memory_order_relaxed);
		bottomDockPhase.store(rollbackSnapshot.phase, memory_order_relaxed);
		bottomDockElasticOffsetDip.store(
			rollbackSnapshot.elasticOffsetDip, memory_order_relaxed);
		bottomDockRecoveryActive.store(
			rollbackSnapshot.recoveryActive, memory_order_relaxed);
		bottomDockCenterMode.store(
			rollbackSnapshot.centerMode, memory_order_relaxed);
		bottomDockCenterPhase.store(
			rollbackSnapshot.centerPhase, memory_order_relaxed);
		bottomDockCenterElasticOffsetDip.store(
			rollbackSnapshot.centerElasticOffsetDip, memory_order_relaxed);
		directWindowDragTranslationX.store(
			rollbackSnapshot.directTranslation.x, memory_order_relaxed);
		directWindowDragTranslationY.store(
			rollbackSnapshot.directTranslation.y, memory_order_relaxed);
		bottomDockDragActive.store(false, memory_order_relaxed);
		bottomDockIndicatorGestureEligible.store(false, memory_order_relaxed);
		(void)FinishBottomDockTransition(true);
	}
	else
	{
		dockTracker.End();
		centerTracker.End();
		const BarBottomDockMode finalMode = downwardDetachBlockedAtRelease
			? BarBottomDockMode::BottomDocked : dockTracker.Mode();
		const double currentVisualOffset =
			BottomDockPresentedSnapshot().elasticOffsetDip;
		const bool needsRecovery = abs(currentVisualOffset)
			> BarBottomDockSettleDistanceDip
			|| abs(dockTracker.ElasticOffsetDip())
				> BarBottomDockSettleDistanceDip;
		const BarBottomDockCenterMode finalCenterMode =
			finalMode == BarBottomDockMode::BottomDocked && !barState.fold
				? centerTracker.Mode() : BarBottomDockCenterMode::Free;
		const bool centerNeedsRecovery = abs(
			centerTracker.ElasticOffsetDip()) > BarBottomDockSettleDistanceDip;
		const bool releaseModeChanged = bottomDockMode.load(
			memory_order_acquire) != finalMode
			|| bottomDockCenterMode.load(memory_order_acquire)
				!= finalCenterMode;
		BeginBottomDockTransition();
		bottomDockMode.store(finalMode, memory_order_release);
		bottomDockPhase.store(needsRecovery
			? BarBottomDockPhase::Recovering
			: BarBottomDockPhase::Stable, memory_order_release);
		bottomDockElasticOffsetDip.store(needsRecovery
			? dockTracker.ElasticOffsetDip() : 0.0, memory_order_release);
		bottomDockRecoveryActive.store(
			finalMode == BarBottomDockMode::Floating && needsRecovery,
			memory_order_release);
		bottomDockCenterMode.store(finalCenterMode, memory_order_release);
		bottomDockCenterPhase.store(centerNeedsRecovery
			? BarBottomDockPhase::Recovering : BarBottomDockPhase::Stable,
			memory_order_release);
		bottomDockCenterElasticOffsetDip.store(centerNeedsRecovery
			? centerTracker.ElasticOffsetDip() : 0.0, memory_order_release);
		bottomDockDragActive.store(false, memory_order_relaxed);
		bottomDockIndicatorGestureEligible.store(false, memory_order_relaxed);
		(void)FinishBottomDockTransition(releaseModeChanged);
	}
	FinishDirectWindowDrag();
	// 松手只发布接管请求；布局值由渲染线程吸收，避免和动画线程并发写对象。
	if (!offSignal) UpdateRendering(false);
	result.allowClick = ShouldAllowBarBottomDockClick(
		directMoveFailed, gestureCancelled, result.modeChanged,
		result.rawPathLength, maximumElasticTravelScreen,
		maximumInteractionZoom);
	POINT releasePoint = BarPointerScreenPoint(pointerMessage);
	if (result.allowClick && BarScreenToLayout(releasePoint)
		&& IsBottomDockIndicatorPresentedAt(releasePoint.x, releasePoint.y))
		result.allowClick = false;
	return result;
}

namespace Inkeys::UI::Bar
{
	void NotifyBorderCursorSurfacePointerEntered() noexcept
	{
		if (!offSignal && floating_window)
			PostMessage(floating_window,
				BarBorderCursorSurfacePointerMessage, 1, 0);
	}

	void NotifyBorderCursorSurfacePointerLeft() noexcept
	{
		if (!offSignal && floating_window)
			PostMessage(floating_window,
				BarBorderCursorSurfacePointerMessage, 0, 0);
	}

	void PublishBorderCursorSurfaceBounds(
		unsigned int index, const RECT& bounds, bool visible) noexcept
	{
		if (index >= borderCursorSurfaceRegions.size()) return;
		const bool valid = visible && bounds.right > bounds.left
			&& bounds.bottom > bounds.top;
		bool leftVisibleSurface = false;
		{
			std::lock_guard lock(borderCursorSurfaceRegionMutex);
			leftVisibleSurface = borderCursorSurfaceRegionVisible[index]
				&& !valid;
			borderCursorSurfaceRegions[index] = valid ? bounds : RECT{};
			borderCursorSurfaceRegionVisible[index] = valid;
		}
		if (leftVisibleSurface) NotifyBorderCursorSurfacePointerLeft();
	}

	Inkeys::Message::Reply QueueWindowMessageInLayoutSpace(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const auto extraInfo = static_cast<ULONG_PTR>(GetMessageExtraInfo());
		if (Inkeys::Message::IsPointerGeneratedMouseMessage(
			message, extraInfo))
			return { Inkeys::Message::Action::Discard, 0 };
		if (!IsBarCoordinateMessage(message)) return {};
		// 普通 Down/Move/Up 始终保留 HiMsg 入口；Raw 只在控件拖动状态机中补充。

		// 在窗口线程入队时就固化屏幕坐标，避免 resize 后用新原点解释旧 client 消息。
		Inkeys::Window::MessageDispatch::Sample posted;
		const bool hasPosted = Inkeys::Window::MessageDispatch::TryGet(
			hwnd, message, wParam, lParam, posted);
		POINT point{};
		if (hasPosted) point = posted.screen;
		else if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
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
		const POINT screenPoint = point;
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
		if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
			(void)Inkeys::Window::Enqueue(hwnd, queued);
		else
		{
			Pointer::Sample sample;
			sample.source = Pointer::Source::Mouse;
			sample.kind = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK
				? Pointer::Kind::Down : message == WM_LBUTTONUP
				? Pointer::Kind::Up : Pointer::Kind::Move;
			if (sample.kind == Pointer::Kind::Down)
				barNativeMouseActiveGeneration = ++barNativeMouseGeneration;
			sample.generation = barNativeMouseActiveGeneration;
			if (sample.kind == Pointer::Kind::Up) barNativeMouseActiveGeneration = 0;
			sample.screen = screenPoint;
			sample.messageTime = hasPosted ? posted.time : static_cast<DWORD>(GetMessageTime());
			sample.tick = Inkeys::Window::MessageDispatch::PostedTick(sample.messageTime);
			(void)EnqueueBarPointerSample(hwnd, queued, sample);
		}
		return { Inkeys::Message::Action::Discard, 0 };
	}

	bool TryQueueColorPickerKeyboardInput(BYTE vkCode, bool keyDown)
	{
		const auto stateMode = GetStateModeSnapshot();
		// 键盘只在指针仍位于本Bar窗口或Bar拥有焦点时路由，避免拦截其他应用。
		POINT pointer{};
		const bool eraserKey = vkCode == VK_ESCAPE || vkCode == VK_TAB || vkCode == VK_LEFT
			|| vkCode == VK_RIGHT || vkCode == VK_RETURN || vkCode == VK_SPACE;
		// 撤回属性复用同一组键盘路由键（Escape/Tab/方向/回车/空格）。
		const bool auxPanelWantsKeyboard = barUISet.eraserAttribute.WantsKeyboard() || barUISet.recallAttribute.WantsKeyboard();
		if (eraserKey && !offSignal && floating_window && auxPanelWantsKeyboard
			&& (GetForegroundWindow() == floating_window || (GetCursorPos(&pointer) && WindowFromPoint(pointer) == floating_window)))
		{
			ExMessage message{}; message.message = keyDown ? WM_KEYDOWN : WM_KEYUP; message.vkcode = vkCode;
			return Inkeys::Window::Enqueue(floating_window, message);
		}
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

	void NotifyCanvasStrokeAccepted(const AcceptedStrokeBegin& notice) noexcept
	{
		if (offSignal) return;
		std::lock_guard lock(barAcceptedStrokeMutex);
		if (barAcceptedStrokeCount == barAcceptedStrokes.size())
		{
			// 候选有界；突发时保留较新的场景，不为每个笔迹分配消息。
			barAcceptedStrokeRead = (barAcceptedStrokeRead + 1) % barAcceptedStrokes.size();
			--barAcceptedStrokeCount;
		}
		barAcceptedStrokes[(barAcceptedStrokeRead + barAcceptedStrokeCount)
			% barAcceptedStrokes.size()] = notice;
		++barAcceptedStrokeCount;
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
