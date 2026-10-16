module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Business/LegacyDrawState.hpp"
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include <cstdio>
#include <d2d1helper.h>
#include "../../Window/Window.Legacy.hpp"
#include "Bar.BottomDock.h"
#include "Bar.I18nFormat.h"
#include "Bar.WindowGeometry.h"
#include "Bar.PresentationProbe.h"
#include "Bar.Presentation.Source.h"
#include "Bar.InteractionWorker.h"
#include <type_traits>
#include <future>
#include <stop_token>

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
import Inkeys.UI.RenderPipeline;
import Inkeys.Window;
import Inkeys.Display;
import Inkeys.UI.MessageBox;
import Inkeys.Startup.Progress;
import Inkeys.Drawing.Draw3.diagnostics;
using Inkeys::UI::Bar::BarToggleChannel;
using Inkeys::UI::Bar::SetBarButtonPressedVisual;
using Inkeys::UI::Bar::StartBarButtonHoverVisual;
using Inkeys::UI::Bar::StopBarButtonHoverVisual;
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
constexpr UINT BarInputReleaseCaptureMessage = WM_APP + 0x36;
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

template<class Shape> bool IsPresentedBarVisualHit(BarUISetClass& owner,
	const std::shared_ptr<Shape>& shape, double clientX, double clientY)
{
	if (!shape) return false;
	for (;;)
	{
		const auto before = owner.BottomDockPresentedSnapshot();
		const double zoom = (std::max)(0.000001, before.zoom);
		const bool hit = owner.HitTestPresentedShape(*shape, clientX / zoom, clientY / zoom);
		if (owner.BottomDockPresentedSnapshot().serial == before.serial) return hit;
	}
}

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
		&& IsPresentedBarVisualHit(barUISet, popupSurface, clientX, clientY);
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

namespace
{
	// 票据仅由原生窗口线程推进；延迟释放必须核对同一接触，不能释放后来建立的捕获。
	struct MainNativeCaptureOwnership
	{
		HWND window = nullptr;
		std::uint64_t serial = 0, ticket = 0;
		std::uint64_t Begin(HWND target) noexcept
		{
			window = target;
			if (++serial == 0) ++serial;
			return ticket = serial;
		}
		bool Owns(HWND target, std::uint64_t expected) const noexcept
		{ return expected != 0 && window == target && ticket == expected; }
		bool Retire(HWND target, std::uint64_t expected) noexcept
		{
			if (!Owns(target, expected)) return false;
			ticket = 0; return true;
		}
		std::uint64_t Current(HWND target) const noexcept
		{ return window == target ? ticket : 0; }
	};
	MainNativeCaptureOwnership mainNativeCaptureOwnership;
	std::atomic_uint64_t mainSliderCaptureTicket = 0, mainColorPickerCaptureTicket = 0;
	std::atomic_uint64_t activeMainGestureCaptureTicket = 0;
	bool UpdateMainAttributeCapture(std::atomic_uint64_t& attribute, std::uint64_t ticket, bool start) noexcept
	{
		if (!ticket) return false;
		if (start)
		{
			// 逻辑接触来自 worker 已消费的 Down；即使 native Up/新 Down 先到也不丢 FIFO 编辑。
			if (activeMainGestureCaptureTicket.load(std::memory_order_acquire) != ticket) return false;
			attribute.store(ticket, std::memory_order_release); return true;
		}
		return attribute.compare_exchange_strong(ticket, 0);
	}
	std::mutex acceptedMainCaptureMutex;
	HWND acceptedMainCaptureWindow = nullptr;
	std::uint64_t acceptedMainCaptureTicket = 0;
	thread_local void (*mainCaptureReleaseTestSink)(HWND, std::uint64_t, void*) = nullptr;
	thread_local void* mainCaptureReleaseTestContext = nullptr;
	void RequestMainCaptureRelease(HWND window, std::uint64_t ticket) noexcept
	{
		if (!window || !ticket) return;
		if (mainCaptureReleaseTestSink) mainCaptureReleaseTestSink(window, ticket, mainCaptureReleaseTestContext);
		else PostMessage(window, BarInputReleaseCaptureMessage, static_cast<WPARAM>(ticket), 0);
	}
	void CancelAcceptedMainCapture() noexcept
	{
		HWND window; std::uint64_t ticket;
		{
			std::lock_guard lock(acceptedMainCaptureMutex);
			window = std::exchange(acceptedMainCaptureWindow, nullptr);
			ticket = std::exchange(acceptedMainCaptureTicket, 0);
		}
		RequestMainCaptureRelease(window, ticket);
	}
	std::uint64_t AcquireBarNativeCapture(HWND window);
	void ReleaseBarNativeCapture(HWND window, std::uint64_t ticket);
	bool EnqueueMainInteractionMessage(HWND window, ExMessage message, std::uint64_t captureTicket = 0);
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
					enqueued = EnqueueMainInteractionMessage(window, input);
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
	using Inkeys::UI::Bar::InteractionTask;
	using Inkeys::UI::Bar::SurfaceInputEvent;
	using InteractionClock = std::chrono::steady_clock;
	struct SharedInputEnvelope
	{
		SurfaceInputEvent surface{};
		ExMessage main{};
		std::shared_ptr<BarButtonClass> mainButtonHit;
		bool mainHitFrozen = false;
		bool fixtureSource = false;
		bool forMain = false;
	};
	struct SurfaceInputRegistration
	{
		HWND window = nullptr;
		std::uint64_t generation = 0;
		Inkeys::UI::Bar::SurfaceInputHandler handler = nullptr;
		Inkeys::UI::Bar::SurfaceInputTick tick = nullptr;
		Inkeys::UI::Bar::SurfaceInputCancel cancel = nullptr;
		void* context = nullptr;
		bool nativeWindow = true;
		std::atomic_bool active = true;
		std::atomic_uint64_t acceptedCaptureTicket=0;
		std::mutex callbackMutex;
		std::condition_variable callbackDrained;
		unsigned inFlight = 0;
		bool cancellationDelivered = false;
	};
	Inkeys::UI::Bar::InteractionMailbox<SharedInputEnvelope> sharedInputMailbox;
	std::mutex interactionRegistrationMutex;
	std::vector<std::shared_ptr<SurfaceInputRegistration>> interactionRegistrations;
	std::atomic_bool interactionWorkerRunning = false;
	std::atomic_bool offscreenInteractionWorker = false;
	std::atomic<DWORD> interactionWorkerThreadId = 0;
	// 仅由交互 worker 使用；非当前 filter 的键盘消息保持有界，手势结束后继续消费。
	std::deque<SharedInputEnvelope> pendingMainInput;
	struct MainInteractionAwaitContext
	{
		std::coroutine_handle<> continuation{};
		ExMessage* output = nullptr;
		BYTE filter = 0;
		bool* received = nullptr;
		InteractionClock::time_point deadline = InteractionClock::time_point::max();
		bool cancelled = false;
		std::shared_ptr<BarButtonClass> currentButtonHit;
		bool currentHitFrozen = false;
		std::uint64_t currentCaptureTicket = 0, gestureCaptureTicket = 0;
	};
	thread_local MainInteractionAwaitContext* mainInteractionAwaitContext = nullptr;
	bool InteractionCancelled() noexcept
	{ return mainInteractionAwaitContext && mainInteractionAwaitContext->cancelled; }
	std::uint64_t MainGestureCaptureTicket() noexcept
	{ return mainInteractionAwaitContext ? mainInteractionAwaitContext->gestureCaptureTicket : 0; }
	void BeginMainGestureCapture() noexcept
	{
		if (mainInteractionAwaitContext)
		{
			mainInteractionAwaitContext->gestureCaptureTicket = mainInteractionAwaitContext->currentCaptureTicket;
			activeMainGestureCaptureTicket.store(mainInteractionAwaitContext->gestureCaptureTicket, std::memory_order_release);
		}
	}

	void FreezeMainPresentedButtonHit(SharedInputEnvelope& event)
	{
		if (!IsBarCoordinateMessage(event.main.message)) return;
		event.mainHitFrozen = true;
		for (;;)
		{
			// mapping 与命中 maps 在同一奇偶发布边界内提交；读者不拼接跨帧的缩放和按钮。
			const auto before = barUISet.BottomDockPresentedSnapshot();
			event.mainButtonHit.reset();
			ExMessage rigid = event.main;
			PrepareBarInteractionMessage(rigid, false);
			ExMessage body = rigid; ApplyBarBottomDockBodyHitTestFromRigid(body);
			const double zoom = (std::max)(0.000001, before.zoom);
			const auto more = barUISet.barButtonSet.GetMoreButtonSnapshot();
			auto hitMore = [&](const auto& buttons)
			{
				for (const auto& button : buttons)
					if (button && barUISet.HitTestPresentedButton(*button, rigid.x / zoom, rigid.y / zoom))
					{ event.mainButtonHit = button; return true; }
				return false;
			};
			const bool moreHit = barUISet.barState.moreExpanded
				&& (hitMore(more.explicitMore) || hitMore(more.forcedOverflow));
			if (!moreHit)
				for (const auto& button : barUISet.barButtonSet.buttonList.Snapshot())
					if (button && barUISet.HitTestPresentedButton(*button, body.x / zoom, body.y / zoom))
					{ event.mainButtonHit = button; break; }
			if (barUISet.BottomDockPresentedSnapshot().serial != before.serial) continue;
			event.surface.presentedRevision = before.serial;
			return;
		}
	}

	bool CanCoalesceMainInput(const SharedInputEnvelope& previous, const SharedInputEnvelope& next) noexcept
	{
		return previous.forMain && next.forMain && !previous.fixtureSource && !next.fixtureSource
			&& previous.surface.window == next.surface.window
			&& previous.main.message == WM_MOUSEMOVE && next.main.message == WM_MOUSEMOVE
			&& previous.main.lbutton == next.main.lbutton && previous.main.rbutton == next.main.rbutton
			&& previous.main.mbutton == next.main.mbutton && previous.main.shift == next.main.shift
			&& previous.main.ctrl == next.main.ctrl && previous.main.wheel == next.main.wheel
			&& previous.surface.captureTicket == next.surface.captureTicket
			&& previous.surface.presentedRevision == next.surface.presentedRevision;
	}

	bool EnqueueMainInteractionMessage(HWND window, ExMessage message, std::uint64_t captureTicket)
	{
		message.hwnd = window;
		if (message.category == 0)
			message.category = (message.message == WM_KEYDOWN || message.message == WM_KEYUP
				|| message.message == WM_SYSKEYDOWN || message.message == WM_SYSKEYUP) ? EM_KEY : EM_MOUSE;
		SharedInputEnvelope event;
		event.forMain = true; event.main = message;
		event.fixtureSource = Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled();
		event.surface.window = window;
		event.surface.message = message.message;
		event.surface.touch = IsBarTouchPointerMessage(message);
		event.surface.captureTicket = captureTicket;
		FreezeMainPresentedButtonHit(event);
		return sharedInputMailbox.Push(std::move(event), CanCoalesceMainInput, [](const SharedInputEnvelope& admitted)
		{
			if ((admitted.main.message == WM_LBUTTONDOWN || admitted.main.message == WM_LBUTTONDBLCLK)
				&& admitted.surface.captureTicket)
			{
				// 与 mailbox 准入同锁发布；溢出时连尚未被协程消费的 Down 也有准确释放票据。
				std::lock_guard lock(acceptedMainCaptureMutex);
				acceptedMainCaptureWindow = admitted.surface.window;
				acceptedMainCaptureTicket = admitted.surface.captureTicket;
			}
			return true;
		});
	}

	struct MainInteractionMessageAwaiter
	{
		ExMessage* output;
		BYTE filter;
		bool preserveTouchScreenCoordinates;
		BarTouchScreenSample* screenSample;
		InteractionClock::time_point deadline;
		bool received = false;
		bool await_ready() const noexcept
		{
			return offSignal || !mainInteractionAwaitContext || mainInteractionAwaitContext->cancelled;
		}
		void await_suspend(std::coroutine_handle<> continuation) noexcept
		{
			auto& context = *mainInteractionAwaitContext;
			context.continuation = continuation; context.output = output;
			context.filter = filter; context.received = &received; context.deadline = deadline;
		}
		bool await_resume()
		{
			if (!received) return false;
			if (!Inkeys::UI::Bar::ObserveAuthorizedFixtureDequeue(FixtureWireFromMessage(*output))) return false;
			PrepareBarInteractionMessage(*output, preserveTouchScreenCoordinates, screenSample);
			return true;
		}
	};

	MainInteractionMessageAwaiter NextBarInteractionMessage(ExMessage& message, BYTE filter, HWND,
		bool preserveTouchScreenCoordinates = false, BarTouchScreenSample* screenSample = nullptr,
		InteractionClock::time_point deadline = InteractionClock::time_point::max())
	{
		return { &message, filter, preserveTouchScreenCoordinates, screenSample, deadline };
	}
	struct MainInteractionDelayAwaiter
	{
		InteractionClock::time_point deadline;
		bool received = false;
		bool await_ready() const noexcept
		{
			return offSignal || !mainInteractionAwaitContext || mainInteractionAwaitContext->cancelled;
		}
		void await_suspend(std::coroutine_handle<> continuation) noexcept
		{
			auto& context = *mainInteractionAwaitContext;
			context.continuation = continuation; context.output = nullptr;
			context.filter = 0; context.received = &received; context.deadline = deadline;
		}
		void await_resume() const noexcept {}
	};
	template<class Rep, class Period> MainInteractionDelayAwaiter InteractionDelay(
		std::chrono::duration<Rep, Period> delay) noexcept
	{
		return { InteractionClock::now() + delay };
	}

	bool ResumeMainInteraction(MainInteractionAwaitContext& context, InteractionClock::time_point now)
	{
		if (!context.continuation) return false;
		bool ready = offSignal || context.cancelled || now >= context.deadline;
		bool received = false;
		if (!offSignal && !context.cancelled && context.output)
		{
			for (auto it = pendingMainInput.begin(); it != pendingMainInput.end(); ++it)
			{
				if ((it->main.category & context.filter) == 0) continue;
				*context.output = it->main;
				context.currentButtonHit = it->mainButtonHit; context.currentHitFrozen = it->mainHitFrozen;
				context.currentCaptureTicket = it->surface.captureTicket;
				pendingMainInput.erase(it); ready = received = true; break;
			}
		}
		if (!ready) return false;
		if (context.received) *context.received = received;
		auto continuation = std::exchange(context.continuation, {});
		context.deadline = InteractionClock::time_point::max();
		continuation.resume();
		return true;
	}

	std::vector<std::shared_ptr<SurfaceInputRegistration>> SnapshotInteractionRegistrations()
	{
		std::lock_guard lock(interactionRegistrationMutex);
		return interactionRegistrations;
	}
	template<class Callback> bool WithSurfaceInputCallback(
		const std::shared_ptr<SurfaceInputRegistration>& registration,
		bool requireActive, bool cancellation, Callback callback)
	{
		{
			std::lock_guard lock(registration->callbackMutex);
			if (requireActive && !registration->active.load(std::memory_order_acquire)) return false;
			if (cancellation && !registration->active.load(std::memory_order_acquire)
				&& registration->cancellationDelivered) return false;
			++registration->inFlight;
		}
		try { callback(); }
		catch (...) { if (IDTLogger) IDTLogger->error("[UI3] 分页输入回调失败，保留取消屏障"); }
		{
			std::lock_guard lock(registration->callbackMutex);
			--registration->inFlight;
			if (cancellation && !registration->active.load(std::memory_order_acquire))
				registration->cancellationDelivered = true;
			registration->callbackDrained.notify_all();
		}
		return true;
	}
	void CancelRegisteredInteractionSurfaces()
	{
		for (const auto& registration : SnapshotInteractionRegistrations())
			(void)WithSurfaceInputCallback(registration, false, true, [&]
			{
				SurfaceInputEvent cancellation;cancellation.window=registration->window;cancellation.message=WM_CANCELMODE;
				cancellation.generation=registration->generation;cancellation.captureTicket=registration->acceptedCaptureTicket.load(std::memory_order_acquire);
				if (registration->cancel) registration->cancel(cancellation, registration->context);
			});
	}
	void DispatchSurfaceInteraction(const SurfaceInputEvent& event)
	{
		for (const auto& registration : SnapshotInteractionRegistrations())
		{
			if (registration->window != event.window) continue;
			if (event.generation != 0 && event.generation != registration->generation) continue;
			const bool cancellation = event.message == WM_CANCELMODE || event.message == WM_CAPTURECHANGED
				|| event.message == WM_DESTROY;
			if (WithSurfaceInputCallback(registration, true, cancellation, [&]
			{
				if (cancellation && !event.nativeCaptureReleased)
				{
					if (registration->cancel) registration->cancel(event, registration->context);
				}
				else if (registration->handler) registration->handler(event, registration->context);
			})) break;
		}
	}
	InteractionClock::time_point TickSurfaceInteractions(InteractionClock::time_point now)
	{
		auto deadline = InteractionClock::time_point::max();
		for (const auto& registration : SnapshotInteractionRegistrations())
		{
			if (!registration->active.load(std::memory_order_acquire))
			{
				(void)WithSurfaceInputCallback(registration, false, true, [&]
				{
					SurfaceInputEvent cancellation;cancellation.window=registration->window;cancellation.message=WM_CANCELMODE;
					cancellation.generation=registration->generation;cancellation.captureTicket=registration->acceptedCaptureTicket.load(std::memory_order_acquire);
					if (registration->cancel) registration->cancel(cancellation, registration->context);
				});
				continue;
			}
			(void)WithSurfaceInputCallback(registration, true, false, [&]
			{
				if (registration->tick) deadline = (std::min)(deadline, registration->tick(now, registration->context));
			});
		}
		{
			std::lock_guard lock(interactionRegistrationMutex);
			std::erase_if(interactionRegistrations, [](const auto& registration)
			{
				std::lock_guard callbackLock(registration->callbackMutex);
				return !registration->active.load(std::memory_order_acquire)
					&& registration->cancellationDelivered && registration->inFlight == 0;
			});
		}
		return deadline;
	}

	void ProcessBarTooltipTimer(HWND hWnd, WPARAM wParam);
	void ProcessBarPointerLeave();

	InteractionClock::time_point PumpSharedInteraction(MainInteractionAwaitContext& context,
		InteractionClock::time_point now)
	{
		if (sharedInputMailbox.OverflowPending())
		{
			struct Complete {~Complete(){sharedInputMailbox.CompleteOverflow();}} complete;
			// 丢掉整个在途接触并显式取消；宁可取消点击，也不能丢 Up 后留下捕获。
			pendingMainInput.clear(); context.cancelled = true;
			CancelAcceptedMainCapture();
			CancelRegisteredInteractionSurfaces();
		}
		SharedInputEnvelope event;
		for (unsigned budget = 0; budget != 32 && sharedInputMailbox.Pop(event); ++budget)
		{
			if (!event.forMain) DispatchSurfaceInteraction(event.surface);
			else if (event.main.message == WM_TIMER) ProcessBarTooltipTimer(event.main.hwnd, event.main.wParam);
			else if (event.main.message == WM_MOUSELEAVE) ProcessBarPointerLeave();
			else if (event.main.message == WM_CANCELMODE || event.main.message == WM_CAPTURECHANGED
				|| event.main.message == WM_DESTROY)
			{
				pendingMainInput.clear(); context.cancelled = true;
			}
			else if (!pendingMainInput.empty() && CanCoalesceMainInput(pendingMainInput.back(), event))
				pendingMainInput.back() = std::move(event);
			else if (pendingMainInput.size() < 512) pendingMainInput.push_back(std::move(event));
			else { pendingMainInput.clear(); context.cancelled = true; CancelAcceptedMainCapture(); }
			(void)ResumeMainInteraction(context, now);
			// 主手势取消后先重建续接，再消费随后接触；不能把新 Down/Up 丢在旧协程后。
			if (context.cancelled) break;
		}
		// 时间事件也通过同一 owner，长按重复不再依赖绘制帧率。
		auto deadline = TickSurfaceInteractions(now);
		for (unsigned budget = 0; budget != 32 && ResumeMainInteraction(context, now); ++budget) {}
		return (std::min)(deadline, context.deadline);
	}

	enum class InteractionBusinessKind { Persistence, Dialog, Component };
	struct InteractionBusinessJob
	{
		std::function<void()> action;
		InteractionBusinessKind kind = InteractionBusinessKind::Persistence;
	};
	std::mutex interactionBusinessMutex;
	std::condition_variable interactionBusinessDrained;
	std::deque<InteractionBusinessJob> interactionBusinessQueue;
	unsigned interactionBusinessInFlight = 0;
	bool interactionBusinessDrainScheduled = false;
	std::stop_source interactionBusinessStop;
	std::atomic_bool interactionMemorySaveRequested = false;
	std::atomic_bool interactionMemorySaveScheduled = false;
	std::stop_token InteractionBusinessCancellation()
	{
		std::lock_guard lock(interactionBusinessMutex);
		return interactionBusinessStop.get_token();
	}
	void CancelInteractionBusiness() noexcept
	{
		std::stop_source cancellation;
		{
			std::lock_guard lock(interactionBusinessMutex);
			cancellation = interactionBusinessStop;
		}
		cancellation.request_stop();
	}
	void DrainInteractionBusiness()
	{
		std::unique_lock lock(interactionBusinessMutex);
		interactionBusinessDrained.wait(lock, []
		{
			return interactionBusinessInFlight == 0 && !interactionBusinessDrainScheduled;
		});
	}
	bool ResetInteractionBusiness()
	{
		std::lock_guard lock(interactionBusinessMutex);
		if (interactionBusinessInFlight != 0 || interactionBusinessDrainScheduled) return false;
		interactionBusinessStop = std::stop_source{};
		return true;
	}
	void CALLBACK DrainInteractionBusinessQueue(PTP_CALLBACK_INSTANCE instance, void*)
	{
		(void)CallbackMayRunLong(instance);
		for (;;)
		{
			// 一个有界 FIFO 由一个 STA drain 执行，Shell/COM 不进入交互或渲染线程。
			const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			for (;;)
			{
				InteractionBusinessJob job;
				bool cancelled = false;
				{
					std::lock_guard lock(interactionBusinessMutex);
					if (interactionBusinessQueue.empty()) break;
					job = std::move(interactionBusinessQueue.front()); interactionBusinessQueue.pop_front();
					cancelled = interactionBusinessStop.stop_requested();
				}
				// 退出后不再启动排队的外部组件；保存仍排空，dialog 自己消费 stop_token。
				if (!cancelled || job.kind != InteractionBusinessKind::Component)
					try { job.action(); } catch (...) {}
				{
					std::lock_guard lock(interactionBusinessMutex);
					--interactionBusinessInFlight;
				}
				sharedInputMailbox.Wake();
			}
			if (SUCCEEDED(apartment)) CoUninitialize();
			{
				std::lock_guard lock(interactionBusinessMutex);
				// apartment 清理期间收到的任务仍由当前 drain 接手，不遗漏通知。
				if (!interactionBusinessQueue.empty()) continue;
				interactionBusinessDrainScheduled = false;
				interactionBusinessDrained.notify_all();
				return;
			}
		}
	}
	bool SubmitInteractionBusiness(std::function<void()> action,
		InteractionBusinessKind kind = InteractionBusinessKind::Persistence)
	{
		std::lock_guard lock(interactionBusinessMutex);
		if (interactionBusinessQueue.size() >= 128
			|| (kind == InteractionBusinessKind::Component && interactionBusinessStop.stop_requested())) return false;
		interactionBusinessQueue.push_back({ std::move(action), kind }); ++interactionBusinessInFlight;
		if (interactionBusinessDrainScheduled) return true;
		interactionBusinessDrainScheduled = true;
		if (!TrySubmitThreadpoolCallback(DrainInteractionBusinessQueue, nullptr, nullptr))
		{
			interactionBusinessQueue.pop_back(); --interactionBusinessInFlight;
			interactionBusinessDrainScheduled = false;
			interactionBusinessDrained.notify_all();
			return false;
		}
		return true;
	}

	void RequestInteractionMemorySave()
	{
		interactionMemorySaveRequested.store(true, std::memory_order_release);
		if (interactionMemorySaveScheduled.exchange(true, std::memory_order_acq_rel)) return;
		if (!SubmitInteractionBusiness([]
		{
			for (;;)
			{
				interactionMemorySaveRequested.store(false, std::memory_order_release);
				(void)SetMemory();
				interactionMemorySaveScheduled.store(false, std::memory_order_release);
				if (!interactionMemorySaveRequested.load(std::memory_order_acquire)
					|| interactionMemorySaveScheduled.exchange(true, std::memory_order_acq_rel)) break;
			}
		})) interactionMemorySaveScheduled.store(false, std::memory_order_release);
	}
	struct InteractionVisualFeedback
	{
		const void* identity = nullptr;
		unsigned slot = 0;
		std::function<void()> apply;
	};
	std::mutex interactionVisualMutex;
	std::vector<InteractionVisualFeedback> interactionVisualFeedback;
	bool interactionVisualDrainPosted = false;
	void QueueInteractionVisualFeedback(const void* identity, unsigned slot, std::function<void()> apply)
	{
		{
			std::lock_guard lock(interactionVisualMutex);
			auto previous = std::find_if(interactionVisualFeedback.begin(), interactionVisualFeedback.end(),
				[&](const auto& value) { return value.identity == identity && value.slot == slot; });
			if (previous != interactionVisualFeedback.end()) previous->apply = std::move(apply);
			else if (interactionVisualFeedback.size() < 1024)
				interactionVisualFeedback.push_back({ identity, slot, std::move(apply) });
			else return;
			if (interactionVisualDrainPosted) return;
			interactionVisualDrainPosted = true;
		}
		// 高频 Move 只更新同一条反馈，RenderPipeline 中至多保留一个 drain 请求。
		if (!Inkeys::UI::RenderPipeline::PostControl([]
		{
			std::vector<InteractionVisualFeedback> feedback;
			{
				std::lock_guard lock(interactionVisualMutex);
				feedback.swap(interactionVisualFeedback);
				interactionVisualDrainPosted = false;
			}
			for (auto& value : feedback) value.apply();
			Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Bar);
		}))
		{
			std::lock_guard lock(interactionVisualMutex);
			interactionVisualDrainPosted = false;
			interactionVisualFeedback.clear();
		}
	}

	bool SetInteractionPenColor(COLORREF color, bool save = true)
	{
		const bool accepted = SetPenColor(color, false);
		if (accepted && save) RequestInteractionMemorySave();
		return accepted;
	}
	bool SetInteractionPenWidthIfRevision(double width, std::uint64_t revision, bool save = true)
	{
		const bool accepted = SetPenWidthIfRevision(width, revision, false);
		if (accepted && save) RequestInteractionMemorySave();
		return accepted;
	}
	bool SetInteractionPenWidthPresetIfRevision(int preset, std::uint64_t revision, bool save = true)
	{
		const bool accepted = SetPenWidthPresetIfRevision(preset, revision, false);
		if (accepted && save) RequestInteractionMemorySave();
		return accepted;
	}

	template<class Action> InteractionTask<std::invoke_result_t<Action>> RunInteractionBusiness(Action action)
	{
		using Result = std::invoke_result_t<Action>;
		struct Completion { std::optional<Result> value; std::atomic_bool ready = false; };
		auto completion = std::make_shared<Completion>();
		if (!SubmitInteractionBusiness([completion, action = std::move(action)]() mutable
		{
			try { completion->value.emplace(action()); }
			catch (...) { completion->value.emplace(); }
			completion->ready.store(true, std::memory_order_release);
		}, InteractionBusinessKind::Dialog)) co_return Result{};
		while (!completion->ready.load(std::memory_order_acquire) && !offSignal && !InteractionCancelled())
			co_await InteractionDelay(std::chrono::milliseconds(8));
		if (!completion->ready.load(std::memory_order_acquire)) co_return Result{};
		co_return std::move(*completion->value);
	}

}

namespace Inkeys::UI::Bar
{
	bool RegisterInteractionSurface(HWND window, std::uint64_t generation, SurfaceInputHandler handler,
		SurfaceInputTick tick, SurfaceInputCancel cancel, void* context, bool nativeWindow)
	{
		if (!window || !handler) return false;
		auto registration = std::make_shared<SurfaceInputRegistration>();
		registration->window = window; registration->generation = generation;
		registration->handler = handler; registration->tick = tick;
		registration->cancel = cancel; registration->context = context; registration->nativeWindow = nativeWindow;
		{
			std::lock_guard lock(interactionRegistrationMutex);
			for (const auto& previous : interactionRegistrations)
				if (previous->window == window)
				{
					std::lock_guard callbackLock(previous->callbackMutex);
					previous->active.store(false, std::memory_order_release);
				}
			interactionRegistrations.push_back(std::move(registration));
		}
		sharedInputMailbox.Wake();
		return true;
	}
	void UnregisterInteractionSurface(HWND window)
	{
		// HWND owner 不等待 worker，避免与 worker 的窗口效果请求形成互锁。
		{
			std::lock_guard lock(interactionRegistrationMutex);
			for (const auto& registration : interactionRegistrations)
				if (registration->window == window)
				{
					std::lock_guard callbackLock(registration->callbackMutex);
					registration->active.store(false, std::memory_order_release);
				}
		}
		sharedInputMailbox.RemoveIf([&](const auto& event) { return !event.forMain && event.surface.window == window; });
		sharedInputMailbox.Wake();
	}
	bool DrainInteractionSurfaceCallbacks() noexcept
	{
		const auto registrations = SnapshotInteractionRegistrations();
		if (std::none_of(registrations.begin(), registrations.end(), [](const auto& registration)
			{ return !registration->active.load(std::memory_order_acquire); })) return true;
		if (interactionWorkerThreadId.load(std::memory_order_acquire) == GetCurrentThreadId()) return false;
		for (const auto& registration : registrations)
			if (!registration->active.load(std::memory_order_acquire)
				&& registration->nativeWindow
				&& GetWindowThreadProcessId(registration->window, nullptr) == GetCurrentThreadId()) return false;
		sharedInputMailbox.Wake();
		for (const auto& registration : registrations)
		{
			std::unique_lock lock(registration->callbackMutex);
			if (registration->active.load(std::memory_order_acquire)) continue;
			if (!interactionWorkerRunning.load(std::memory_order_acquire))
			{
				registration->callbackDrained.wait(lock, [&] { return registration->inFlight == 0; });
				registration->cancellationDelivered = true;
			}
			else registration->callbackDrained.wait(lock, [&]
			{
				return registration->inFlight == 0 && (registration->cancellationDelivered
					|| !interactionWorkerRunning.load(std::memory_order_acquire));
			});
		}
		return true;
	}

	bool QueueInteractionComponentAction(std::function<void()> action)
	{
		return !offscreenInteractionWorker.load(std::memory_order_acquire)
			&& action && SubmitInteractionBusiness(std::move(action), InteractionBusinessKind::Component);
	}

	bool SubmitSurfaceInput(const SurfaceInputEvent& input)
	{
		std::shared_ptr<SurfaceInputRegistration> registration;
		{
			std::lock_guard lock(interactionRegistrationMutex);
			for(const auto& candidate:interactionRegistrations)if(candidate->window==input.window
				&&candidate->active.load(std::memory_order_acquire)&&(input.generation==0||input.generation==candidate->generation)){
				registration=candidate;break;
			}
		}
		if(!registration)return false;
		SharedInputEnvelope event; event.surface = input;
		return sharedInputMailbox.Push(std::move(event), [](const auto& previous, const auto& next)
		{
			return !previous.forMain && !next.forMain && previous.surface.window == next.surface.window
				&& previous.surface.message == WM_MOUSEMOVE && next.surface.message == WM_MOUSEMOVE
				&& previous.surface.generation == next.surface.generation
				&& previous.surface.presentedRevision == next.surface.presentedRevision
				&& previous.surface.semanticRevision == next.surface.semanticRevision
				&& previous.surface.captureTicket == next.surface.captureTicket
				&& previous.surface.contactId == next.surface.contactId && previous.surface.touch == next.surface.touch
				&& previous.surface.wParam == next.surface.wParam;
		},[registration](auto& admitted){
			std::lock_guard lock(registration->callbackMutex);
			if(!registration->active.load(std::memory_order_acquire))return false;
			auto& input=admitted.surface;
			const bool cancel=input.message==WM_CANCELMODE||input.message==WM_CAPTURECHANGED||input.message==WM_DESTROY;
			if(cancel&&!input.nativeCaptureReleased&&!input.captureTicket)
				input.captureTicket=registration->acceptedCaptureTicket.load(std::memory_order_acquire);
			else if(!cancel&&input.captureTicket)registration->acceptedCaptureTicket.store(input.captureTicket,std::memory_order_release);
			return true;
		});
	}
	void WakeInteractionWorker() noexcept { sharedInputMailbox.Wake(); }
	struct ScopedInteractionWorker::State
	{
		std::jthread thread;
		std::mutex readyMutex;
		std::condition_variable ready;
		bool started = false;
	};
	ScopedInteractionWorker::ScopedInteractionWorker() = default;
	ScopedInteractionWorker::~ScopedInteractionWorker()
	{
		if (!state_) return;
		state_->thread.request_stop(); sharedInputMailbox.Wake();
		if (state_->thread.joinable()) state_->thread.join();
	}
	std::unique_ptr<ScopedInteractionWorker> CreateOffscreenInteractionWorker()
	{
		if (Inkeys::Window::GetService().Running() || Inkeys::Drawing::Draw3::ProductRunning() || offSignal) return {};
		bool expected = false;
		if (!interactionWorkerRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return {};
		try
		{
			if (!SnapshotInteractionRegistrations().empty())
			{ interactionWorkerRunning.store(false, std::memory_order_release); return {}; }
			auto scope = std::unique_ptr<ScopedInteractionWorker>(new ScopedInteractionWorker);
			scope->state_ = std::make_unique<ScopedInteractionWorker::State>();
			auto* state = scope->state_.get();
			pendingMainInput.clear(); sharedInputMailbox.RemoveIf([](const auto&) { return true; });
			(void)sharedInputMailbox.ConsumeOverflow();
			offscreenInteractionWorker.store(true, std::memory_order_release);
			state->thread = std::jthread([state](std::stop_token stop)
			{
				MainInteractionAwaitContext context;
				mainInteractionAwaitContext = &context;
				interactionWorkerThreadId.store(GetCurrentThreadId(), std::memory_order_release);
				{
					std::lock_guard lock(state->readyMutex); state->started = true; state->ready.notify_all();
				}
				try
				{
					while (!stop.stop_requested())
					{
						const auto observed = sharedInputMailbox.Revision();
						const auto deadline = PumpSharedInteraction(context, InteractionClock::now());
						if (!stop.stop_requested()) sharedInputMailbox.WaitUntil(deadline, observed);
					}
				}
				catch (...) { if (IDTLogger) IDTLogger->error("[UI3] 无窗口输入 owner 退出"); }
				// scope 结束时关闭所有未注销目标，等待中的 drain 得到确定完成通知。
				for (const auto& registration : SnapshotInteractionRegistrations())
				{
					std::lock_guard lock(registration->callbackMutex);
					registration->active.store(false, std::memory_order_release);
				}
				CancelRegisteredInteractionSurfaces();
				(void)TickSurfaceInteractions(InteractionClock::now());
				pendingMainInput.clear(); mainInteractionAwaitContext = nullptr;
				interactionWorkerThreadId.store(0, std::memory_order_release);
				offscreenInteractionWorker.store(false, std::memory_order_release);
				interactionWorkerRunning.store(false, std::memory_order_release);
			});
			std::unique_lock readyLock(state->readyMutex);
			state->ready.wait(readyLock, [&] { return state->started; });
			return scope;
		}
		catch (...)
		{
			offscreenInteractionWorker.store(false, std::memory_order_release);
			interactionWorkerRunning.store(false, std::memory_order_release);
			return {};
		}
	}

	int RunSharedInteractionWorkerTests() noexcept
	{
		if (interactionWorkerRunning.load(std::memory_order_acquire)) return 2;
		int failures = 0;
		const auto check = [&](bool value, const char* name)
		{
			if (!value) { ++failures; std::fprintf(stderr, "[UI3Input] FAIL %s\n", name); }
		};
		struct Probe
		{
			unsigned down = 0, up = 0, cancelled = 0, logicalCancelled = 0, repeat = 0;
			std::uint64_t cancelledCaptureTicket=0;
			bool throwOnCancel=false;
			bool held = false;
			InteractionClock::time_point next = InteractionClock::time_point::max();
		};
		std::array<Probe, 4> probes{};
		const auto handler = +[](const SurfaceInputEvent& input, void* opaque)
		{
			auto& probe = *static_cast<Probe*>(opaque);
			if (input.message == WM_LBUTTONDOWN)
			{
				++probe.down; probe.held = true;
				probe.next = InteractionClock::now() + std::chrono::milliseconds(10);
			}
			if (input.message == WM_LBUTTONUP) { ++probe.up; probe.held = false; }
			if (input.message == WM_CANCELMODE || input.message == WM_CAPTURECHANGED)
			{ ++probe.logicalCancelled; probe.held = false; }
		};
		const auto tick = +[](InteractionClock::time_point now, void* opaque)
		{
			auto& probe = *static_cast<Probe*>(opaque);
			if (probe.held && now >= probe.next)
			{
				++probe.repeat; probe.next = now + std::chrono::milliseconds(10);
			}
			return probe.held ? probe.next : InteractionClock::time_point::max();
		};
		const auto cancel = +[](const SurfaceInputEvent& input, void* opaque)
		{
			auto& probe = *static_cast<Probe*>(opaque);
			++probe.cancelled; probe.held = false;
			probe.cancelledCaptureTicket=input.captureTicket;if(probe.throwOnCancel)throw 1;
		};
		MainInteractionAwaitContext context;
		auto* previousContext = mainInteractionAwaitContext;
		mainInteractionAwaitContext = &context;
		try
		{
			{
				std::lock_guard lock(interactionRegistrationMutex);
				if (!interactionRegistrations.empty())
				{ mainInteractionAwaitContext = previousContext; return 2; }
			}
			for (std::size_t index = 0; index != probes.size(); ++index)
				check(RegisterInteractionSurface(reinterpret_cast<HWND>(index + 1), 7,
					handler, tick, cancel, &probes[index]), "register-production-surface");
			SharedInputEnvelope moveA, moveB;
			moveA.forMain = moveB.forMain = true;
			moveA.main.message = moveB.main.message = WM_MOUSEMOVE;
			check(CanCoalesceMainInput(moveA, moveB), "production-ordinary-moves-coalesce");
			moveB.fixtureSource = true;
			check(!CanCoalesceMainInput(moveA, moveB), "authorized-fixture-wire-provenance-retained");
			moveB.fixtureSource = false; moveB.main.ctrl = true;
			check(!CanCoalesceMainInput(moveA, moveB), "modifier-change-is-an-input-boundary");
			bool receivedMain = false;
			auto waitForMain = [&]() -> InteractionTask<bool>
			{
				ExMessage message{};
				receivedMain = co_await NextBarInteractionMessage(message, EM_KEY, nullptr);
				co_return receivedMain;
			};
			auto mainTask = waitForMain(); mainTask.Start();
			for (std::size_t index = 0; index != probes.size(); ++index)
			{
				SurfaceInputEvent event; event.window = reinterpret_cast<HWND>(index + 1);
				event.generation = 7; event.message = WM_LBUTTONDOWN;
				check(SubmitSurfaceInput(event), "enqueue-production-surface");
			}
			(void)PumpSharedInteraction(context, InteractionClock::now());
			check(!mainTask.Done() && !receivedMain, "main-wait-genuinely-suspends");
			for (const auto& probe : probes) check(probe.down == 1, "all-targets-progress-during-main-wait");
			(void)PumpSharedInteraction(context, InteractionClock::now() + std::chrono::seconds(1));
			for (const auto& probe : probes) check(probe.repeat == 1, "late-deadline-emits-once-without-render");
			SurfaceInputEvent oldNativeCancel;oldNativeCancel.window=reinterpret_cast<HWND>(1);
			oldNativeCancel.generation=7;oldNativeCancel.message=WM_CANCELMODE;oldNativeCancel.captureTicket=11;oldNativeCancel.nativeCaptureReleased=true;
			SurfaceInputEvent nextContact=oldNativeCancel;nextContact.message=WM_LBUTTONDOWN;
			nextContact.captureTicket=12;nextContact.nativeCaptureReleased=false;
			(void)SubmitSurfaceInput(oldNativeCancel);(void)SubmitSurfaceInput(nextContact);
			(void)PumpSharedInteraction(context,InteractionClock::now());
			check(probes[0].logicalCancelled==1&&probes[0].cancelled==0&&probes[0].held,
				"native-cancel-before-new-contact-does-not-repeat-capture-release");
			SurfaceInputEvent stale; stale.window = reinterpret_cast<HWND>(1);
			stale.generation = 6; stale.message = WM_LBUTTONUP;
			(void)SubmitSurfaceInput(stale);
			(void)PumpSharedInteraction(context, InteractionClock::now());
			check(probes[0].up == 0, "old-surface-generation-rejected");
			ExMessage key{}; key.category = EM_KEY; key.message = WM_KEYDOWN; key.vkcode = VK_ESCAPE;
			check(EnqueueMainInteractionMessage(nullptr, key), "enqueue-main-same-mailbox");
			(void)PumpSharedInteraction(context, InteractionClock::now());
			check(mainTask.Done() && receivedMain, "main-resumes-after-other-targets");
			mainTask.Rethrow();
			context = {}; receivedMain = false;
			auto cancelledTask = waitForMain(); cancelledTask.Start();
			std::array<unsigned,4> downBeforeOverflow{};std::array<std::uint64_t,4> droppedCaptureTickets{};
			for(std::size_t i=0;i<probes.size();++i)downBeforeOverflow[i]=probes[i].down;
			for (unsigned index = 0; index != 512; ++index)
			{
				SurfaceInputEvent event; event.window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(index % 4 + 1));
				event.generation = 7; event.message = WM_LBUTTONDOWN;
				event.captureTicket=1000+index;droppedCaptureTickets[index%4]=event.captureTicket;
				check(SubmitSurfaceInput(event), "production-capacity-admitted");
			}
			SurfaceInputEvent overflow; overflow.window = reinterpret_cast<HWND>(1);
			overflow.generation = 7; overflow.message = WM_LBUTTONUP;
			overflow.captureTicket=9000;
			check(!SubmitSurfaceInput(overflow), "production-overflow-detected");
			SurfaceInputEvent rejectedDown=overflow;rejectedDown.message=WM_LBUTTONDOWN;rejectedDown.captureTicket=9001;
			check(!SubmitSurfaceInput(rejectedDown)&&sharedInputMailbox.OverflowPending(),"overflow-barrier-rejects-new-contact-before-cancellation");
			probes[3].throwOnCancel=true;
			(void)PumpSharedInteraction(context, InteractionClock::now());
			probes[3].throwOnCancel=false;
			check(cancelledTask.Done() && !receivedMain, "overflow-cancels-suspended-main");
			for (const auto& probe : probes)
				check(probe.cancelled == 1 && !probe.held, "overflow-cancels-all-affected-targets");
			for(std::size_t i=0;i<probes.size();++i)
				check(probes[i].down==downBeforeOverflow[i]&&probes[i].cancelledCaptureTicket==droppedCaptureTickets[i],
					"unseen-dropped-down-retains-bounded-exact-capture-ticket");
			check(!sharedInputMailbox.OverflowPending(),"overflow-barrier-completes-even-after-cancel-callback-throws");
			rejectedDown.captureTicket=9002;check(SubmitSurfaceInput(rejectedDown),"new-contact-admitted-after-overflow-cancellation");
			(void)PumpSharedInteraction(context,InteractionClock::now());
			check(probes[0].held&&probes[0].down==downBeforeOverflow[0]+1,"post-barrier-contact-progresses-through-real-pump");
			rejectedDown.message=WM_LBUTTONUP;(void)SubmitSurfaceInput(rejectedDown);(void)PumpSharedInteraction(context,InteractionClock::now());
			SurfaceInputEvent unseenDown=rejectedDown;unseenDown.window=reinterpret_cast<HWND>(2);unseenDown.message=WM_LBUTTONDOWN;unseenDown.captureTicket=10001;
			const auto beforeRetire=probes[1].down;check(SubmitSurfaceInput(unseenDown),"unseen-contact-admitted-before-retirement");
			UnregisterInteractionSurface(unseenDown.window);(void)PumpSharedInteraction(context,InteractionClock::now());
			check(probes[1].down==beforeRetire&&probes[1].cancelledCaptureTicket==10001,
				"retirement-retains-capture-ticket-of-unconsumed-down");
			for (std::size_t index = 0; index != probes.size(); ++index)
				UnregisterInteractionSurface(reinterpret_cast<HWND>(index + 1));
			(void)PumpSharedInteraction(context, InteractionClock::now());
			check(SnapshotInteractionRegistrations().empty(), "retired-surface-descriptors-reclaimed");
			{
				struct CaptureProbe
				{
					MainNativeCaptureOwnership owner;
					std::vector<std::uint64_t> requests, observedDowns;
					unsigned retired = 0;
					std::atomic_uint64_t attribute = 0;
					bool completed = false;
				} capture;
				const auto target = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x500));
				struct RestoreCaptureSink
				{
					decltype(mainCaptureReleaseTestSink) sink = mainCaptureReleaseTestSink;
					void* context = mainCaptureReleaseTestContext;
					~RestoreCaptureSink()
					{ mainCaptureReleaseTestSink = sink; mainCaptureReleaseTestContext = context; }
				} restore;
				mainCaptureReleaseTestContext = &capture;
				mainCaptureReleaseTestSink = +[](HWND window, std::uint64_t ticket, void* opaque)
				{
					auto& probe = *static_cast<CaptureProbe*>(opaque);
					probe.requests.push_back(ticket);
					if (probe.owner.Retire(window, ticket)) ++probe.retired;
				};
				// 运行真实 mailbox/Pump：旧协程的取消完成晚于下一接触的原生捕获建立。
				auto gesture = [&]() -> InteractionTask<bool>
				{
					ExMessage input{};
					if (!(co_await NextBarInteractionMessage(input, EM_MOUSE, target))) co_return false;
					BeginMainGestureCapture();
					capture.observedDowns.push_back(MainGestureCaptureTicket());
					const bool started = UpdateMainAttributeCapture(capture.attribute, MainGestureCaptureTicket(), true);
					const bool up = co_await NextBarInteractionMessage(input, EM_MOUSE, target);
					const bool stopped = UpdateMainAttributeCapture(capture.attribute, MainGestureCaptureTicket(), false);
					RequestMainCaptureRelease(target, MainGestureCaptureTicket());
					capture.completed = started && stopped && up && input.message == WM_LBUTTONUP;
					co_return capture.completed;
				};
				context = {};
				auto oldGesture = gesture(); oldGesture.Start();
				ExMessage down{}; down.message = WM_LBUTTONDOWN; down.lbutton = true;
				const auto oldTicket = capture.owner.Begin(target);
				check(EnqueueMainInteractionMessage(target, down, oldTicket), "main-native-down-retains-capture-ticket");
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(!oldGesture.Done() && MainGestureCaptureTicket() == oldTicket, "main-gesture-freezes-its-starting-ticket");
				(void)capture.owner.Retire(target, oldTicket);
				ExMessage cancelled{}; cancelled.message = WM_CANCELMODE;
				check(EnqueueMainInteractionMessage(target, cancelled, oldTicket), "enqueue-old-main-cancel");
				const auto nextTicket = capture.owner.Begin(target);
				check(EnqueueMainInteractionMessage(target, down, nextTicket), "enqueue-next-main-down-before-old-coroutine-finishes");
				ExMessage up{}; up.message = WM_LBUTTONUP;
				check(EnqueueMainInteractionMessage(target, up, nextTicket), "enqueue-next-main-up-in-same-pump-batch");
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(oldGesture.Done() && !capture.completed && capture.owner.Owns(target, nextTicket)
					&& capture.requests == std::vector<std::uint64_t>{ oldTicket }, "old-main-cleanup-cannot-release-new-capture");
				context = {};
				auto nextGesture = gesture(); nextGesture.Start();
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(nextGesture.Done() && capture.completed
					&& capture.observedDowns == std::vector<std::uint64_t>{ oldTicket, nextTicket }
					&& capture.retired == 1, "cancel-boundary-preserves-following-main-down-up");
				CancelAcceptedMainCapture();
				context = {};
				const auto fastTicket = capture.owner.Begin(target);
				check(EnqueueMainInteractionMessage(target, down, fastTicket)
					&& EnqueueMainInteractionMessage(target, up, fastTicket), "native-fast-tap-queued-before-property-start");
				(void)capture.owner.Retire(target, fastTicket);
				const auto replacementTicket = capture.owner.Begin(target);
				check(EnqueueMainInteractionMessage(target, down, replacementTicket), "native-next-down-precedes-first-property-start");
				auto fastGesture = gesture(); fastGesture.Start();
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(fastGesture.Done() && capture.completed && capture.owner.Owns(target, replacementTicket),
					"queued-fast-edit-commits-without-reacquiring-or-releasing-next-native-capture");
				context = {};
				auto replacementGesture = gesture(); replacementGesture.Start();
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(!replacementGesture.Done() && capture.attribute.load() == replacementTicket,
					"queued-second-down-survives-first-gesture-completion");
				check(!UpdateMainAttributeCapture(capture.attribute, fastTicket, false)
					&& capture.attribute.load() == replacementTicket, "old-attribute-stop-cannot-clear-replacement-state");
				(void)capture.owner.Retire(target, replacementTicket);
				check(EnqueueMainInteractionMessage(target, up, replacementTicket), "enqueue-replacement-up");
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(replacementGesture.Done() && capture.completed && capture.observedDowns.size() == 4,
					"native-queued-taps-preserve-both-logical-property-edits");
				CancelAcceptedMainCapture();
				context = {}; receivedMain = false;
				auto unseenMain = waitForMain(); unseenMain.Start();
				const auto admittedTicket = capture.owner.Begin(target);
				for (unsigned i = 0; i != 512; ++i)
					check(EnqueueMainInteractionMessage(target, down, admittedTicket), "main-overflow-fill-admitted");
				const auto rejectedTicket = capture.owner.Begin(target);
				check(!EnqueueMainInteractionMessage(target, down, rejectedTicket), "main-overflow-rejects-next-native-down");
				// 原生拒绝路径立即按被拒票据释放，不能覆盖 mailbox 已准入的票据。
				check(capture.owner.Retire(target, rejectedTicket), "rejected-main-down-releases-only-its-own-ticket");
				(void)PumpSharedInteraction(context, InteractionClock::now());
				check(unseenMain.Done() && !receivedMain && !capture.requests.empty()
					&& capture.requests.back() == admittedTicket && capture.observedDowns.size() == 4,
					"main-overflow-releases-exact-ticket-of-unconsumed-down");
				check(!sharedInputMailbox.OverflowPending(), "main-overflow-barrier-completes-after-release-request");
				activeMainGestureCaptureTicket.store(0, std::memory_order_release);
				context = {};
			}
			check(ResetInteractionBusiness(), "business-service-starts-empty");
			std::vector<unsigned> businessOrder;
			std::promise<void> entered; auto enteredResult = entered.get_future();
			const auto token = InteractionBusinessCancellation();
			check(SubmitInteractionBusiness([&]
			{
				businessOrder.push_back(1); entered.set_value();
				std::mutex mutex; std::condition_variable_any wake; std::unique_lock lock(mutex);
				(void)wake.wait(lock, token, [] { return false; });
				businessOrder.push_back(2);
			}, InteractionBusinessKind::Dialog), "cooperative-business-request-accepted");
			check(enteredResult.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
				"business-drain-started");
			check(SubmitInteractionBusiness([&] { businessOrder.push_back(3); }), "accepted-save-queued-after-modal-request");
			check(QueueInteractionComponentAction([&] { businessOrder.push_back(4); }), "external-action-queued-in-order");
			CancelInteractionBusiness(); DrainInteractionBusiness();
			check(businessOrder == std::vector<unsigned>{ 1, 2, 3 }, "shutdown-cancels-dialog-drains-save-skips-pending-launch");
			check(!QueueInteractionComponentAction([] {}), "shutdown-rejects-new-external-action");
			check(ResetInteractionBusiness(), "business-context-fully-joined-before-reuse");
			{
				auto scope = CreateOffscreenInteractionWorker();
				check(scope != nullptr, "offscreen-factory-starts-production-pump");
				if (scope)
				{
					check(!CreateOffscreenInteractionWorker(), "second-interaction-owner-rejected");
					struct WakeProbe { std::mutex mutex; std::condition_variable wake; unsigned calls = 0; } wakeProbe;
					const auto input = +[](const SurfaceInputEvent&, void* value)
					{
						auto& probe = *static_cast<WakeProbe*>(value);
						std::lock_guard lock(probe.mutex); ++probe.calls; probe.wake.notify_all();
					};
					const auto opaque = reinterpret_cast<HWND>(0x100);
					check(RegisterInteractionSurface(opaque, 9, input, nullptr, nullptr, &wakeProbe, false), "opaque-target-registers-without-native-owner");
					SurfaceInputEvent event; event.window = opaque; event.generation = 9; event.message = WM_LBUTTONDOWN;
					check(SubmitSurfaceInput(event), "opaque-target-uses-production-mailbox");
					{
						std::unique_lock lock(wakeProbe.mutex);
						check(wakeProbe.wake.wait_for(lock, std::chrono::seconds(1), [&] { return wakeProbe.calls == 1; }), "production-worker-dispatches-opaque-input");
					}
					UnregisterInteractionSurface(opaque);
					check(DrainInteractionSurfaceCallbacks(), "opaque-callbacks-drain-before-context-destruction");
				}
			}
			check(!interactionWorkerRunning.load(std::memory_order_acquire), "scope-stop-wakes-and-joins-worker");


		}
		catch (...) { ++failures; std::fputs("[UI3Input] FAIL exception\n", stderr); }
		{
			std::lock_guard lock(interactionRegistrationMutex);
			interactionRegistrations.clear();
		}
		sharedInputMailbox.RemoveIf([](const auto&) { return true; });
		pendingMainInput.clear(); mainInteractionAwaitContext = previousContext;
		std::fprintf(stderr, "[UI3Input] %s: production mailbox/dispatcher/continuation/deadline/cancel\n",
			failures ? "FAIL" : "PASS");
		return failures ? 1 : 0;
	}

}

void ClearBarInteractionMessages(BYTE filter, HWND hWnd)
{
	std::size_t removed = sharedInputMailbox.RemoveIf([&](const auto& event)
	{
		return event.forMain && event.surface.window == hWnd && (event.main.category & filter) != 0;
	});
	for (auto it = pendingMainInput.begin(); it != pendingMainInput.end();)
		if (it->main.hwnd == hWnd && (it->main.category & filter) != 0) { it = pendingMainInput.erase(it); ++removed; }
		else ++it;
	Inkeys::UI::Bar::ObserveAuthorizedFixtureClear(filter, removed);
}

void QueueBarThicknessSliderEnd(HWND hWnd, std::uint64_t captureTicket)
{
	if (Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
	{
		Inkeys::UI::Bar::RejectAuthorizedFixtureSource(Inkeys::UI::Bar::Ui3FixtureSourceFailure::UnsupportedState);
		return;
	}
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

	(void)EnqueueMainInteractionMessage(hWnd, message, captureTicket);
}

void QueueBarColorPickerEnd(HWND hWnd, std::uint64_t captureTicket)
{
	if (Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
	{
		Inkeys::UI::Bar::RejectAuthorizedFixtureSource(Inkeys::UI::Bar::Ui3FixtureSourceFailure::UnsupportedState);
		return;
	}
	if (!hWnd) return;
	POINT point{};
	if (!GetCursorPos(&point)) point = {};
	BarScreenToLayout(point);

	ExMessage message{};
	message.message = WM_LBUTTONUP;
	message.x = static_cast<short>(clamp<LONG>(point.x, SHRT_MIN, SHRT_MAX));
	message.y = static_cast<short>(clamp<LONG>(point.y, SHRT_MIN, SHRT_MAX));
	message.lbutton = false;

	(void)EnqueueMainInteractionMessage(hWnd, message, captureTicket);
}

// ====================
// 窗口

namespace
{
	void ProcessBarTooltipTimer(HWND hWnd, WPARAM wParam)
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
			return;

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
				&& IsPresentedBarVisualHit(barUISet, colorPickerPanel, point.x, point.y);
			bool pointerInside = pointAvailable && available
				&& !colorPickerOccludes
				&& ((infoHit && IsPresentedBarVisualHit(barUISet, infoHit, point.x, point.y))
					|| (popupInteractive && popup && IsPresentedBarVisualHit(barUISet, popup, point.x, point.y)));
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
				&& ((sliderHit && IsPresentedBarVisualHit(barUISet, sliderHit, point.x, point.y))
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
		return;
	}
	void ProcessBarPointerLeave()
	{
		if (barUISet.eraserAttribute.ResetPointerFeedback()) barUISet.UpdateRendering(false);
		if (barUISet.recallAttribute.ResetPointerFeedback()) barUISet.UpdateRendering(false);
		// 原生窗口线程统一处理离开反馈，解除边缘光效的重新激活限制。
		Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerLeft();
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
		return;

	}

}

namespace
{
	bool voluntaryBarCaptureRelease = false;
	std::uint64_t AcquireBarNativeCapture(HWND window)
	{
		SetCapture(window);
		return GetCapture() == window ? mainNativeCaptureOwnership.Begin(window) : 0;
	}
	void ReleaseBarNativeCapture(HWND window, std::uint64_t ticket)
	{
		if (!mainNativeCaptureOwnership.Retire(window, ticket) || GetCapture() != window) return;
		voluntaryBarCaptureRelease = true;
		ReleaseCapture();
		voluntaryBarCaptureRelease = false;
	}
}

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == BarInputReleaseCaptureMessage)
	{
		ReleaseBarNativeCapture(hWnd, static_cast<std::uint64_t>(wParam));
		return 0;
	}
	if (msg == WM_DESTROY)
	{
		ExMessage cancel{}; cancel.message = WM_DESTROY; (void)EnqueueMainInteractionMessage(hWnd, cancel, mainNativeCaptureOwnership.Current(hWnd));
		barUISet.ShutdownWindowInput(hWnd);
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
	if (msg == WM_CANCELMODE)
	{
		ExMessage cancel{}; cancel.message = WM_CANCELMODE; (void)EnqueueMainInteractionMessage(hWnd, cancel, mainNativeCaptureOwnership.Current(hWnd));
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
		Inkeys::Drawing::Draw3::RecordCursorRawInput(lParam);
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
			ExMessage timer{}; timer.message = WM_TIMER; timer.category = EM_WINDOW; timer.wParam = wParam;
			(void)EnqueueMainInteractionMessage(hWnd, timer);
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
		auto ticket = static_cast<std::uint64_t>(lParam);
		if (wParam == BarThicknessSliderCaptureStart)
		{
			// 复用 Down 在窗口线程建立的捕获；滞后的属性 Start 不能接管新接触。
			if (!UpdateMainAttributeCapture(mainSliderCaptureTicket, ticket, true)) return 0;
			barUISet.barState.drawAttributeBar.thicknessSliderCapture = true;
			return 1;
		}

		if (!UpdateMainAttributeCapture(mainSliderCaptureTicket, ticket, false)) return 0;
		barUISet.barState.drawAttributeBar.thicknessSliderCapture = false;
		ReleaseBarNativeCapture(hWnd, ticket);
		if (wParam == BarThicknessSliderCaptureCancel)
			QueueBarThicknessSliderEnd(hWnd, ticket);
		return 0;
	}

	case BarColorPickerCaptureMessage:
	{
		auto ticket = static_cast<std::uint64_t>(lParam);
		if (wParam == BarColorPickerCaptureStart)
		{
			// 与粗细调节一致，只接受本次 Down 的票据，允许拖出色板继续夹紧选色。
			if (!UpdateMainAttributeCapture(mainColorPickerCaptureTicket, ticket, true)) return 0;
			barUISet.barState.drawAttributeBar.colorPickerPointerCapture = true;
			return 1;
		}

		if (!UpdateMainAttributeCapture(mainColorPickerCaptureTicket, ticket, false)) return 0;
		barUISet.barState.drawAttributeBar.colorPickerPointerCapture = false;
		ReleaseBarNativeCapture(hWnd, ticket);
		if (wParam == BarColorPickerCaptureCancel)
			QueueBarColorPickerEnd(hWnd, ticket);
		return 0;
	}

	case WM_CAPTURECHANGED:
	{
		if (!voluntaryBarCaptureRelease)
		{
			const auto ticket = mainNativeCaptureOwnership.Current(hWnd);
			(void)mainNativeCaptureOwnership.Retire(hWnd, ticket);
			ExMessage cancel{}; cancel.message = WM_CAPTURECHANGED;
			(void)EnqueueMainInteractionMessage(hWnd, cancel, ticket);
		}
		return 0;
	}

	case WM_MOUSELEAVE:
	{
		ExMessage leave{}; leave.message = WM_MOUSELEAVE; leave.category = EM_WINDOW;
		(void)EnqueueMainInteractionMessage(hWnd, leave);
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

							const auto ticket = mainNativeCaptureOwnership.Current(hWnd);
							(void)EnqueueMainInteractionMessage(hWnd, msgMouse, ticket);
							ReleaseBarNativeCapture(hWnd, ticket);
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

							const auto ticket = AcquireBarNativeCapture(hWnd);
							if (!EnqueueMainInteractionMessage(hWnd, msgMouse, ticket))
							{ ReleaseBarNativeCapture(hWnd, ticket); Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false); }
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

							(void)EnqueueMainInteractionMessage(hWnd, msgMouse, mainNativeCaptureOwnership.Current(hWnd));
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

							const auto ticket = mainNativeCaptureOwnership.Current(hWnd);
							(void)EnqueueMainInteractionMessage(hWnd, msgMouse, ticket);
							ReleaseBarNativeCapture(hWnd, ticket);
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
		if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
		{
			Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, true);
		}
		if (msg == WM_LBUTTONUP)
		{
			Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);
		}

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
			static_cast<LPARAM>(mainInteractionAwaitContext ? MainGestureCaptureTicket()
				: mainSliderCaptureTicket.load(std::memory_order_acquire)));
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
				static_cast<LPARAM>(mainInteractionAwaitContext ? MainGestureCaptureTicket()
					: mainColorPickerCaptureTicket.load(std::memory_order_acquire)));
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


	if (!hWnd) return;

	// 先撤销输入源，避免 ReleaseCapture 重入时继续向交互队列投递手势。
	KillTimer(hWnd, BarBorderCursorGraceTimerId);
	KillTimer(hWnd, BarThicknessAnnotationTooltipGraceTimerId);
	KillTimer(hWnd, BarThicknessOverflowTooltipGraceTimerId);
	KillTimer(hWnd, BarRecallChromeIdleTimerId);
	barState.drawAttributeBar.thicknessSliderCapture = false;
	barState.drawAttributeBar.colorPickerPointerCapture = false;
	ReleaseBarNativeCapture(hWnd, mainNativeCaptureOwnership.Current(hWnd));
	SuspendBorderCursorTracking(hWnd);
}

namespace
{
struct BarInteractionMemberAccess
{
	InteractionTask<BarSeekResult> (BarUISetClass::*seek)(const ExMessage&) = nullptr;
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

	std::shared_ptr<BarButtonClass> RetainButton(BarButtonClass* button)
	{
		if (!button) return {};
		for (const auto& candidate : barButtonSet.buttonList.Snapshot())
			if (candidate.get() == button) return candidate;
		const auto more = barButtonSet.GetMoreButtonSnapshot();
		for (const auto& candidate : more.explicitMore) if (candidate.get() == button) return candidate;
		for (const auto& candidate : more.forcedOverflow) if (candidate.get() == button) return candidate;
		return {};
	}
	bool HitPresentedButton(const BarButtonClass& button, const ExMessage& message)
	{
		if (mainInteractionAwaitContext && mainInteractionAwaitContext->currentHitFrozen)
			return mainInteractionAwaitContext->currentButtonHit.get() == &button;
		const double zoom = (std::max)(0.000001, barUISet.BottomDockPresentedSnapshot().zoom);
		return barUISet.HitTestPresentedButton(button, message.x / zoom, message.y / zoom);
	}
	void SetButtonPressed(BarButtonClass& button, bool pressed)
	{
		auto retained = RetainButton(&button);
		if (!retained) return;
		QueueInteractionVisualFeedback(&button, 1, [retained = std::move(retained), pressed]
		{
			SetBarButtonPressedVisual(*retained, pressed);
			Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Bar);
		});
	}
	void StartHover(BarUiPctClass* pct, BarUiColorClass* fill,
		IdtAtomic<BarButtonHoverStageEnum>* stage)
	{
		QueueInteractionVisualFeedback(pct, 0, [pct, fill, stage]
		{
			if (Inkeys::UI::Bar::StartBarButtonHoverVisual(pct, fill, stage))
				Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Bar);
		});
	}
	void StopHover(BarUiPctClass* pct, BarUiColorClass* fill,
		IdtAtomic<BarButtonHoverStageEnum>* stage, bool immediate, bool preserveVisual = false)
	{
		QueueInteractionVisualFeedback(pct, 0, [pct, fill, stage, immediate, preserveVisual]
		{
			if (Inkeys::UI::Bar::StopBarButtonHoverVisual(pct, fill, stage, immediate, preserveVisual))
				Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Bar);
		});
	}
	void StartMainBarButtonHover(BarButtonClass* button)
	{
		auto retained = RetainButton(button);
		if (!retained) return;
		QueueInteractionVisualFeedback(button, 0, [retained = std::move(retained)]
		{
			if (StartBarButtonHoverVisual(*retained))
				Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Bar);
		});
	}
	void StopMainBarButtonHover(BarButtonClass* button, bool immediate, bool preserveVisual = false)
	{
		auto retained = RetainButton(button);
		if (!retained) return;
		QueueInteractionVisualFeedback(button, 0, [retained = std::move(retained), immediate, preserveVisual]
		{
			if (StopBarButtonHoverVisual(*retained, immediate, preserveVisual))
				Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Client::Bar);
		});
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
				if (SetInteractionPenWidthIfRevision(candidate, thicknessFineDialModeRevision, setMemory))
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
			&& IsPresentedBarVisualHit(barUISet, pickerPanel, clientX, clientY);
	}

	bool IsPenTypeMenuOccludingPoint(int clientX, int clientY)
	{
		if (!barState.drawAttributeBar.penTypeMenuOpen)
			return false;
		auto menu = shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu];
		return menu && IsPresentedBarVisualHit(barUISet, menu, clientX, clientY);
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
			SetInteractionPenColor(Inkeys::Color::SetAlphaR(color, 255), setMemory);
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
					SetInteractionPenColor(GetPenColor(), true);
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

	InteractionTask<bool> RunColorPickerPointerGesture(ExMessage& gestureMessage)
		{
			if (gestureMessage.message != WM_LBUTTONDOWN
				|| !ColorPickerAvailable()
				|| !barState.drawAttributeBar.colorPickerOpen)
				co_return false;
			auto palette = shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette];
			if (!palette || palette->w.val <= 0.0 || palette->h.val <= 0.0)
				co_return false;

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
			BarColorPickerCaptureStart, static_cast<LPARAM>(MainGestureCaptureTicket()));
		UpdateRendering();

		while (!offSignal && !InteractionCancelled())
		{
			const bool hasInput = co_await NextBarInteractionMessage(gestureMessage, EM_MOUSE, floating_window,
				false, nullptr, InteractionClock::now() + std::chrono::milliseconds(8));
			stateMode = GetStateModeSnapshot();
			if (!hasInput)
			{
				if (!ColorPickerAvailable() || !picker.colorPickerOpen
					|| (!Inkeys::Inputs::IsKeyBoardDown(VK_LBUTTON)
						&& !gestureMessage.lbutton))
					break;
				// 触摸不会保证同步系统鼠标位置，静止期间沿用最后一个触点坐标。
				UpdateHold(activeScreenPoint);
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
				BarColorPickerCaptureStop, static_cast<LPARAM>(MainGestureCaptureTicket()));
		bool completed = captured && !offSignal
			&& !gestureMessage.lbutton && ColorPickerAvailable()
			&& picker.colorPickerOpen;
		picker.colorPickerPointerPressed = false;
		picker.colorPickerHoldHintActive = false;
		picker.colorPickerHoldLocked = false;
		picker.colorPickerHoldProgress = 0.0f;
		if (completed)
			SetInteractionPenColor(Inkeys::Color::SetAlphaR(finalColor, 255), true);
		UpdateRendering();
		SuppressHoverUntilPointerMove();
		ClearBarInteractionMessages(EM_MOUSE, floating_window);
		co_return true;
	}

	BarInteractionStageResult FinishInteraction()
	{
		if (hoveredMainBarButton) StopMainBarButtonHover(hoveredMainBarButton, true);
		if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
			StopIndependentHover(hoveredIndependentButton, true);
		for (const auto& button : barButtonSet.buttonList.Snapshot())
			if (button) SetButtonPressed(*button, false);
		const auto more = barButtonSet.GetMoreButtonSnapshot();
		for (const auto& button : more.explicitMore) if (button) SetButtonPressed(*button, false);
		for (const auto& button : more.forcedOverflow) if (button) SetButtonPressed(*button, false);
		auto& draw = barState.drawAttributeBar;
		draw.brush1Press = draw.softPenPress = draw.laserPress = draw.highlight1Press = false;
		draw.penTypeExtensionPress = draw.penTypeFreeLinePress = false;
		draw.thicknessFinePress = draw.thicknessMediumPress = draw.thicknessCoarsePress = draw.thicknessAdjustPress = false;
		draw.thicknessAnnotationClosePress = draw.thicknessOverflowClosePress = false;
		draw.colorPickerTonePress = draw.colorPickerClosePress = false;
		auto& geometry = barState.geometryAttributeBar;
		geometry.straightLinePress = geometry.rectanglePress = geometry.closePress = false;
		geometry.thicknessFinePress = geometry.thicknessMediumPress = geometry.thicknessCoarsePress = false;
		barState.moreClosePress = false; BarColorPickerEntryPressed = false;
		barUISet.eraserAttribute.ResetPointerFeedback(); barUISet.recallAttribute.ResetPointerFeedback();
		CancelThicknessFineDialSelection();
		CloseThicknessSlider(true); CloseColorPicker(true); ClosePenTypeMenu();
		RequestMainCaptureRelease(floating_window, MainGestureCaptureTicket());
		auto ticket = MainGestureCaptureTicket();
		(void)activeMainGestureCaptureTicket.compare_exchange_strong(ticket, 0);
		UpdateRendering(false);
		return BarInteractionStageResult::Shutdown;
	}

	InteractionTask<BarInteractionStageResult> PollInteractionMessage()
	{
		currentTouchScreenSample.ready = false;
		if ((thicknessFineDialPhase == ThicknessFineDialPhase::Inertia
			|| thicknessFineDialPhase == ThicknessFineDialPhase::Settling)
			&& !barState.drawAttributeBar.thicknessFineDialPhysicsActive)
		{
			thicknessFineDialPhase = ThicknessFineDialPhase::Idle;
			thicknessFineDialVelocity = thicknessFineDialResidualVelocity = 0.0;
			ResetThicknessFineDialSamples();
		}
		const bool physics = barState.drawAttributeBar.thicknessFineDialPhysicsActive
			&& (thicknessFineDialPhase == ThicknessFineDialPhase::Inertia
				|| thicknessFineDialPhase == ThicknessFineDialPhase::Settling);
		const auto deadline = physics ? InteractionClock::now() + BarThicknessFineDialPhysicsPollInterval
			: InteractionClock::time_point::max();
		const bool received = co_await NextBarInteractionMessage(msg, EM_MOUSE | EM_KEY,
			floating_window, false, &currentTouchScreenSample, deadline);
		if (offSignal || InteractionCancelled()) co_return BarInteractionStageResult::Shutdown;
		if (!received)
		{
			AdvanceThicknessFineDialPhysics();
			co_return BarInteractionStageResult::Consumed;
		}
		if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK) BeginMainGestureCapture();
		if (physics) thicknessFineDialPhysicsClockNeedsReset = true;
		co_return BarInteractionStageResult::PassThrough;
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
		for (const auto& retainedButton : barButtonSet.buttonList.Snapshot())
		{
			auto button = retainedButton.get();
			if (button && button->state
				&& button->state->emph == BarWidgetEmphasize::Pressed)
				SetButtonPressed(*button, false);
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
		RequestMainCaptureRelease(floating_window, MainGestureCaptureTicket());
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
		if (const auto retainedHover = RetainButton(hoveredMainBarButton); hoveredMainBarButton
			&& (barState.fold || !retainedHover || !retainedHover->IsVisible()))
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
					&& IsPresentedBarVisualHit(barUISet, annotationInfoHit, msg.x, msg.y);
				bool overflowBadgeHover = !colorPickerOccludes
					&& !penTypeMenuOccludes
					&& OverflowTooltipAvailable()
					&& overflowInfoHit
					&& IsPresentedBarVisualHit(barUISet, overflowInfoHit, msg.x, msg.y);
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
					&& IsPresentedBarVisualHit(barUISet, annotationPopup, msg.x, msg.y);
				bool overflowPopupHover = !colorPickerOccludes
					&& !penTypeMenuOccludes
					&& !annotationPopupHover
					&& OverflowTooltipAvailable()
					&& overflowPopupInteractive && overflowPopup
					&& IsPresentedBarVisualHit(barUISet, overflowPopup, msg.x, msg.y);
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
					&& ((sliderHit && IsPresentedBarVisualHit(barUISet, sliderHit, msg.x, msg.y))
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
				// 普通按钮与 More 共用入队时锁存的成功呈现命中。
				if (!barState.fold && !colorPickerOccludes
					&& !penTypeMenuOccludes)
				{
					for (const auto& retainedButton : barButtonSet.buttonList.Snapshot())
					{
						BarButtonClass* temp = retainedButton.get();
						if (!temp || !temp->IsVisible()
							|| temp->preset == BarButtonPresetEnum::Divider
							|| temp->state->state == BarWidgetState::Disable
							|| temp->state->state == BarWidgetState::Selected) continue;
						bool isColorSelector = temp->name.enable.tar
							&& temp->name.content.GetTar().substr(0, 7) == L"__color";
						if (isColorSelector) continue; // 颜色块自身就是内容，不把其填充色改成悬停灰色。
						if (HitPresentedButton(*temp, msg))
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
									if (HitPresentedButton(*button, msg))
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
					if (freeLine && IsPresentedBarVisualHit(barUISet, freeLine, msg.x, msg.y))
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
					if (extension && IsPresentedBarVisualHit(barUISet, extension, msg.x, msg.y))
						currentIndependentButton = IndependentHoverTargetEnum::
							DrawAttributePenTypeExtension;
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None
					&& !penTypeMenuOccludes
					&& IsIndependentHoverAllowed(IndependentHoverTargetEnum::MoreClose))
				{
					auto moreClose = shapeMap[BarUISetShapeEnum::MorePanelCloseHit];
					if (moreClose && IsPresentedBarVisualHit(barUISet, moreClose, msg.x, msg.y))
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
						&& IsPresentedBarVisualHit(barUISet, colorPickerCloseHit, msg.x, msg.y))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeColorPickerClose;
					}
					else if (IsIndependentHoverAllowed(
						IndependentHoverTargetEnum::DrawAttributeColorPickerTone)
						&& colorPickerToneHit
						&& IsPresentedBarVisualHit(barUISet, colorPickerToneHit, msg.x, msg.y))
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
							&& IsPresentedBarVisualHit(barUISet, shape, msg.x, msg.y))
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
					&& IsPresentedBarVisualHit(barUISet, annotationClose, msg.x, msg.y))
				{
					currentIndependentButton =
						IndependentHoverTargetEnum::DrawAttributeAnnotationClose;
				}
				else if (IsIndependentHoverAllowed(
					IndependentHoverTargetEnum::DrawAttributeOverflowClose)
					&& overflowClose
					&& IsPresentedBarVisualHit(barUISet, overflowClose, msg.x, msg.y))
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
						&& obj && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
						&& obj && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeThicknessAdjust;
				}
				if (currentIndependentButton == IndependentHoverTargetEnum::None)
				{
					if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Laser];
						obj && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeLaser;
					}
					else if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1];
					stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHardPen
					&& obj && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeBrush;
					}
					else if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_SoftPen];
						stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenSoftPen
						&& obj && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
					{
						currentIndependentButton =
							IndependentHoverTargetEnum::DrawAttributeSoftPen;
					}
					else if (auto obj =
						shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1];
						stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1
						&& obj && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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

	InteractionTask<BarInteractionStageResult> HandleColorPickerPointerStage()
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
				bool insidePicker = pickerPanel && IsPresentedBarVisualHit(barUISet, pickerPanel, msg.x, msg.y);
				bool insideEntry = customSwatch && IsPresentedBarVisualHit(barUISet, customSwatch, msg.x, msg.y);
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
				if (pickerClose && IsPresentedBarVisualHit(barUISet, pickerClose, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (!IsPresentedBarVisualHit(barUISet, pickerClose, msg.x, msg.y)) break;
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
				else if (pickerTone && IsPresentedBarVisualHit(barUISet, pickerTone, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (!IsPresentedBarVisualHit(barUISet, pickerTone, msg.x, msg.y)) break;
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
				else if (pickerPalette && IsPresentedBarVisualHit(barUISet, pickerPalette, msg.x, msg.y))
				{
					continueFlag = false;
					(void)co_await RunColorPickerPointerGesture(msg);
				}
				else if (pickerPanel && IsPresentedBarVisualHit(barUISet, pickerPanel, msg.x, msg.y))
				{
					// 面板正文阻止点击穿透；面板外则维持既有主栏交互。
					continueFlag = false;
				}
			}

			if (continueFlag && ColorPickerAvailable())
			{
				auto customSwatch = shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
				if (customSwatch && IsPresentedBarVisualHit(barUISet, customSwatch, msg.x, msg.y))
				{
					continueFlag = false;
					if (msg.message == WM_LBUTTONDOWN)
					{
						BarColorPickerEntryPressed = true;
						UpdateRendering(false);
						while (true)
						{
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (!IsPresentedBarVisualHit(barUISet, customSwatch, msg.x, msg.y)) break;
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
			co_return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> HandleOverlayPointerStage()
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
					|| !IsPresentedBarVisualHit(barUISet, closeHit, msg.x, msg.y))
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
						if (!(co_await AwaitNextBarInteractionMessage(
							msg, EM_MOUSE, floating_window)))
						{
							co_return BarInteractionStageResult::Shutdown;
						}
						if (IsPresentedBarVisualHit(barUISet, closeHit, msg.x, msg.y))
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
					|| !IsPresentedBarVisualHit(barUISet, infoHit, msg.x, msg.y))
					continue;

				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					CancelTooltipHoverGrace(
						*tooltip.hoverGrace, tooltip.hoverGraceTimerId);
					while (true)
					{
						if (!(co_await AwaitNextBarInteractionMessage(
							msg, EM_MOUSE, floating_window)))
						{
							co_return BarInteractionStageResult::Shutdown;
						}
						if (IsPresentedBarVisualHit(barUISet, infoHit, msg.x, msg.y))
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
					&& IsPresentedBarVisualHit(barUISet, popup, msg.x, msg.y))
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
				bool insideMenu = menu && IsPresentedBarVisualHit(barUISet, menu, msg.x, msg.y);
				bool insideEntry = entry && IsPresentedBarVisualHit(barUISet, entry, msg.x, msg.y);
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
				bool insideFreeLine = freeLine && IsPresentedBarVisualHit(barUISet, freeLine, msg.x, msg.y);
				bool insideAnnotationLine = annotationLine && IsPresentedBarVisualHit(barUISet, annotationLine, msg.x, msg.y);
				bool insideMenu = menu && IsPresentedBarVisualHit(barUISet, menu, msg.x, msg.y);
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (!IsPresentedBarVisualHit(barUISet, freeLine, msg.x, msg.y)) break;
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
			co_return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> HandleMorePointerStage()
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
					&& IsPresentedBarVisualHit(barUISet, morePanel, msg.x, msg.y);
				bool pointerInMoreButton = moreButton
					&& HitPresentedButton(*moreButton, msg);

				if (moreClose && IsPresentedBarVisualHit(barUISet, moreClose, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (!IsPresentedBarVisualHit(barUISet, moreClose, msg.x, msg.y)) break;
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
							|| !HitPresentedButton(*button, msg)) continue;
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN)
						{
							Inkeys::UI::Bar::ButtonPointerState pointerState;
							const auto pointerIdentity = reinterpret_cast<std::uintptr_t>(button.get());
							(void)pointerState.Update(Inkeys::UI::Bar::ButtonPointerEvent::Down, pointerIdentity);
							SetButtonPressed(*button, true);
							StopMainBarButtonHover(hoveredMainBarButton, true, true);
							hoveredMainBarButton = nullptr;
							UpdateRendering(false);
							while (true)
							{
								if (!(co_await AwaitNextBarInteractionMessage(
									msg, EM_MOUSE, floating_window)))
								{
									co_return BarInteractionStageResult::Shutdown;
								}
								const auto pointer = pointerState.Update(msg.message == WM_LBUTTONUP
									? Inkeys::UI::Bar::ButtonPointerEvent::Up : Inkeys::UI::Bar::ButtonPointerEvent::Move,
									HitPresentedButton(*button, msg) ? pointerIdentity : 0);
								if (pointer.pressed == 0 && pointer.activate == 0) break;
								if (msg.message == WM_LBUTTONUP && pointer.activate == pointerIdentity)
								{
									// 默认先收起再执行，避免动作打开新窗口时浮层残留。
									if (button->closeMoreAfterAction)
										barState.moreExpanded = false;
									if (button->clickFunc) button->clickFunc();
									break;
								}
							}
							SetButtonPressed(*button, false);
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
			co_return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> HandleMainButtonAndBarPointerStage()
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
			if (auto obj = superellipseMap[BarUISetSuperellipseEnum::MainButton]; continueFlag && IsPresentedBarVisualHit(barUISet, obj, gripMessage.x, gripMessage.y))
			{
				continueFlag = false;
				if (msg.message == WM_LBUTTONDOWN)
				{
					if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(Inkeys::UI::Bar::Ui3FiniteScene::MainFold,
						Inkeys::UI::Bar::Ui3FixtureActionPoint::Down)) co_return BarInteractionStageResult::Shutdown;
					// Seek 需要按下时的真实视觉坐标，不能把主体逆形变坐标当作抓取点。
					ExMessage seekMessage = msg;
					seekMessage.x = static_cast<short>(clamp(
						visualMessageX, static_cast<int>(SHRT_MIN),
						static_cast<int>(SHRT_MAX)));
					seekMessage.y = static_cast<short>(clamp(
						visualMessageY, static_cast<int>(SHRT_MIN),
						static_cast<int>(SHRT_MAX)));
					const BarSeekResult seekResult = co_await Seek(seekMessage);
					if (seekResult.allowClick)
					{
						if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(Inkeys::UI::Bar::Ui3FiniteScene::MainFold,
							Inkeys::UI::Bar::Ui3FixtureActionPoint::Commit)) co_return BarInteractionStageResult::Shutdown;
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
					const bool shouldClose = co_await RunInteractionBusiness([cancellation = InteractionBusinessCancellation()]() -> bool
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
					request.cancellation = cancellation;
					request.labels.ok = okLabel.c_str();
					request.labels.cancel = cancelLabel.c_str();
					request.owner = floating_window;
					request.requireOwner = true;
					request.fallback.owner = floating_window;
					request.fallback.modality =
						Inkeys::UI::MessageBox::SystemModality::System;
					return Inkeys::UI::MessageBox::Show(request) == Inkeys::UI::MessageBox::Result::Ok;
					});
					if (shouldClose && !offSignal && !InteractionCancelled()) CloseProgram();

					ClearBarInteractionMessages(EM_MOUSE, floating_window);
				}
			}

			// 按钮
			if (continueFlag)
			{
				// 特殊体质：按钮
				const auto mainButtons = barButtonSet.buttonList.Snapshot();
				for (const auto& retainedButton : mainButtons)
				{
					BarButtonClass* temp = retainedButton.get();
					if (temp == nullptr || !temp->IsVisible()
						|| temp->preset == BarButtonPresetEnum::Divider
						|| temp->state->state == BarWidgetState::Disable) continue;

					// 双击第二击仍归属于第一击按钮，避免动画中按钮位移导致命中丢失。
					bool doubleClickContinuation = msg.message == WM_LBUTTONDBLCLK
						&& temp == lastClickedMainBarButton;
					if (HitPresentedButton(*temp, msg) || doubleClickContinuation)
					{
						continueFlag = false;
						if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK)
						{
							const auto fixtureAction = temp->preset == BarButtonPresetEnum::Draw
								? Inkeys::UI::Bar::Ui3FiniteScene::DrawAttribute : static_cast<Inkeys::UI::Bar::Ui3FiniteScene>(0);
							if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(fixtureAction,
								Inkeys::UI::Bar::Ui3FixtureActionPoint::Down)) co_return BarInteractionStageResult::Shutdown;
							bool clickCompleted = false;
							// 同一背景层先切换到按下状态；抬起后必须收到新的鼠标移动才能再次悬停。
							Inkeys::UI::Bar::ButtonPointerState pointerState;
							const auto pointerIdentity = reinterpret_cast<std::uintptr_t>(temp);
							(void)pointerState.Update(Inkeys::UI::Bar::ButtonPointerEvent::Down, pointerIdentity);
							SetButtonPressed(*temp, true);
							StopMainBarButtonHover(hoveredMainBarButton, true, true);
							hoveredMainBarButton = nullptr;
							UpdateRendering(false);
							while (true)
							{
								if (!(co_await AwaitNextBarInteractionMessage(
									msg, EM_MOUSE, floating_window)))
								{
									co_return BarInteractionStageResult::Shutdown;
								}
								ApplyBarBottomDockBodyHitTestFromRigid(msg);
								if (doubleClickContinuation || HitPresentedButton(*temp, msg))
								{
									// Move 缺少 MK_LBUTTON 不能代表抬起，点击只在明确 Up 后执行。
									if (msg.message == WM_LBUTTONUP && !msg.lbutton
										&& pointerState.Update(Inkeys::UI::Bar::ButtonPointerEvent::Up, pointerIdentity).activate == pointerIdentity)
									{
										if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(fixtureAction,
											Inkeys::UI::Bar::Ui3FixtureActionPoint::Commit)) co_return BarInteractionStageResult::Shutdown;
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
							SetButtonPressed(*temp, false); UpdateRendering(false);
							SuppressHoverUntilPointerMove();

							// 成功点击后保留队列中的下一击；拖出取消时仍清理本轮残留消息。
							if (!clickCompleted) ClearBarInteractionMessages(EM_MOUSE, floating_window);
						}
						break;
					}
				}
			}
			msg.y = rigidMessageY;
			co_return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> HandleGeometryPointerStage()
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
					if (!shape || !IsPresentedBarVisualHit(barUISet, shape, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (!IsPresentedBarVisualHit(barUISet, shape, msg.x, msg.y)) break;
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
								else SetInteractionPenWidthPresetIfRevision(button.thicknessPresetIndex,
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
			co_return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> HandleThicknessAndFineDialPointerStage()
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
						if ((sliderHit && IsPresentedBarVisualHit(barUISet, sliderHit, msg.x, msg.y))
							|| previewPopupHit
							|| fineActivationHit
								!= BarThicknessFineDialHitZone::None)
						{
							continueFlag = false;
							if (msg.message == WM_LBUTTONDOWN
								&& !fineActivationCorridorConsumed
								&& !rangeTransitionConsumesPress)
							{
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
									&& IsPresentedBarVisualHit(barUISet, sliderThumb, msg.x, msg.y);
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
										BarThicknessSliderCaptureStart, static_cast<LPARAM>(MainGestureCaptureTicket()));
									UpdateRendering(false);

									while (!offSignal && !InteractionCancelled())
									{
										// 等待输入或最近的计时期限；挂起期间其他窗口仍由同一 worker 处理。
										const bool hasInput = co_await NextBarInteractionMessage(msg, EM_MOUSE, floating_window, false,
											nullptr, InteractionClock::now() + std::chrono::milliseconds(8));
										stateMode = GetStateModeSnapshot();
										if (!hasInput)
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
											BarThicknessSliderCaptureStop, static_cast<LPARAM>(MainGestureCaptureTicket()));
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
												SetInteractionPenWidthIfRevision(finalWidth, gestureRevision, true);
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
											SetInteractionPenWidthIfRevision(finalWidth, gestureRevision, true);
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
										(annotationInfoHit && IsPresentedBarVisualHit(barUISet, annotationInfoHit, msg.x, msg.y))
										|| (annotationPopupInteractive && annotationPopup
											&& IsPresentedBarVisualHit(barUISet, annotationPopup, msg.x, msg.y));
					bool overflowPointerInside = !annotationPointerInside
						&& ((overflowInfoHit && IsPresentedBarVisualHit(barUISet, overflowInfoHit, msg.x, msg.y))
							|| (overflowPopupInteractive && overflowPopup
								&& IsPresentedBarVisualHit(barUISet, overflowPopup, msg.x, msg.y)));
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
											&& ((sliderHit && IsPresentedBarVisualHit(barUISet, sliderHit, msg.x, msg.y))
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
			co_return continueFlag
				? BarInteractionStageResult::PassThrough
				: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> HandleDrawAttributePointerStage()
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
						if (swatch && IsPresentedBarVisualHit(barUISet, swatch, msg.x, msg.y))
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
								SetInteractionPenColor(Inkeys::Color::SetAlphaR(
									swatch->fill.value().tar, 255));
								if (barState.drawAttributeBar.colorPickerOpen)
									ProjectCurrentColorPickerPoint();
								UpdateRendering();
							};
						ApplySwatch(activeSwatch);

						// 只有色块内的 Down 才启动拖选，穿过间隙后仍可切到新色块。
						while (true)
						{
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
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
									|| !IsPresentedBarVisualHit(barUISet, swatch, msg.x, msg.y))
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
								|| !IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
									if (!(co_await AwaitNextBarInteractionMessage(
										msg, EM_MOUSE, floating_window)))
									{
										co_return BarInteractionStageResult::Shutdown;
									}
									if (IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
												SetInteractionPenWidthPresetIfRevision(button.presetIndex, toolRevisionAtPress);
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
					if (extension && IsPresentedBarVisualHit(barUISet, extension, msg.x, msg.y))
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
								if (!(co_await AwaitNextBarInteractionMessage(
									msg, EM_MOUSE, floating_window)))
								{
									co_return BarInteractionStageResult::Shutdown;
								}
								if (!IsPresentedBarVisualHit(barUISet, extension, msg.x, msg.y)) break;
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
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Laser]; continueFlag && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(msg, EM_MOUSE, floating_window)))
								co_return BarInteractionStageResult::Shutdown;
							if (IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y) && !msg.lbutton)
							{
								clickCompleted = true;
								ClosePenTypeMenu();
								CloseThicknessSlider(true);
								ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
								UpdateRendering();
								break;
							}
							if (!IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y)) break;
						}
						barState.drawAttributeBar.laserPress = false;
						UpdateRendering(false);
						if (clickCompleted) SuppressHoverUntilPointerMove();
						ClearBarInteractionMessages(EM_MOUSE, floating_window);
					}
				}

				// 画笔
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]; continueFlag && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
					continueFlag && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
								co_return BarInteractionStageResult::Shutdown;
							if (IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
				if (auto obj = shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1]; continueFlag && IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
							if (!(co_await AwaitNextBarInteractionMessage(
								msg, EM_MOUSE, floating_window)))
							{
								co_return BarInteractionStageResult::Shutdown;
							}
							if (IsPresentedBarVisualHit(barUISet, obj, msg.x, msg.y))
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
		co_return continueFlag
			? BarInteractionStageResult::PassThrough
			: BarInteractionStageResult::Consumed;
	}

	InteractionTask<BarInteractionStageResult> DispatchPointerStages()
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
			const auto result = co_await (this->*stage)();
			if (stage == &BarInteractionSession::HandleMainButtonAndBarPointerStage
				&& result == BarInteractionStageResult::PassThrough && Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
			{
				Inkeys::UI::Bar::RejectAuthorizedFixtureSource(Inkeys::UI::Bar::Ui3FixtureSourceFailure::Action);
				co_return BarInteractionStageResult::Shutdown;
			}
			if (result != BarInteractionStageResult::PassThrough)
				co_return result;
		}
		co_return BarInteractionStageResult::PassThrough;
	}

public:
	InteractionTask<bool> Run()
	{
		Inkeys::UI::Bar::EnterAuthorizedFixtureInteraction();
		struct FixtureInteractionExit { ~FixtureInteractionExit() { Inkeys::UI::Bar::LeaveAuthorizedFixtureInteraction(); } } fixtureExit;
		// 私有pure latch独立于Startup tracker；默认observer不存在时不工作。
		Inkeys::UI::Bar::NotifyFixtureInteractionReady();
		// Session 构造、mailbox 与交互状态全部可用后再报告 ready。
		(void)Inkeys::Startup::Report(
			Inkeys::Startup::Milestone::BarInteractionReady);
		while (!offSignal && !InteractionCancelled())
		{
			const auto pollResult = co_await PollInteractionMessage();
			stateMode = GetStateModeSnapshot();
			if (pollResult == BarInteractionStageResult::Shutdown) break;
			if (pollResult == BarInteractionStageResult::Consumed) continue;
			if (Inkeys::UI::Bar::AuthorizedUi3FixtureSourceInstalled())
			{
				if (IsBarTouchCancelMessage(msg)) continue;
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

			if (barUISet.eraserAttribute.Pointer(barUISet, msg, IsBarTouchCancelMessage(msg), IsBarTouchPointerMessage(msg)))
			{
				if (hoveredMainBarButton) { StopMainBarButtonHover(hoveredMainBarButton, true); hoveredMainBarButton = nullptr; }
				if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
				{ StopIndependentHover(hoveredIndependentButton, true); hoveredIndependentButton = IndependentHoverTargetEnum::None; }
				continue;
			}
			// 撤回属性与橡皮同级早处理；二者互斥，同时最多一个展开。
			if (barUISet.recallAttribute.Pointer(barUISet, msg, IsBarTouchCancelMessage(msg), IsBarTouchPointerMessage(msg)))
			{
				if (hoveredMainBarButton) { StopMainBarButtonHover(hoveredMainBarButton, true); hoveredMainBarButton = nullptr; }
				if (hoveredIndependentButton != IndependentHoverTargetEnum::None)
				{ StopIndependentHover(hoveredIndependentButton, true); hoveredIndependentButton = IndependentHoverTargetEnum::None; }
				continue;
			}
			const auto hoverResult = HandleCommonHoverAndOcclusion();
			if (hoverResult == BarInteractionStageResult::Shutdown) break;
			if (hoverResult == BarInteractionStageResult::Consumed) continue;

			const auto pointerResult = co_await DispatchPointerStages();
			if (pointerResult == BarInteractionStageResult::Shutdown) break;
			if (pointerResult == BarInteractionStageResult::Consumed) continue;
		}
		(void)FinishInteraction();
		co_return true;
	}

private:
	InteractionTask<bool> AwaitNextBarInteractionMessage(ExMessage& message, BYTE filter, HWND hWnd,
		bool preserveTouchScreenCoordinates = false,
		BarTouchScreenSample* touchScreenSample = nullptr)
	{
		const bool received = co_await NextBarInteractionMessage(message, filter, hWnd,
			preserveTouchScreenCoordinates, touchScreenSample);
		if (received) stateMode = GetStateModeSnapshot();
		co_return received;
	}

	void UpdateRendering(bool updateState = true)
	{
		barUISet.UpdateRendering(updateState);
	}

	InteractionTask<BarSeekResult> Seek(const ExMessage& message)
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
		co_return co_await (barUISet.*memberAccess.seek)(seekMessage);
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
	bool expected = false;
	if (!interactionWorkerRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
	(void)ResetInteractionBusiness();
	MainInteractionAwaitContext context;
	mainInteractionAwaitContext = &context;
	interactionWorkerThreadId.store(GetCurrentThreadId(), std::memory_order_release);
	{
		std::unique_ptr<BarInteractionSession> session;
		InteractionTask<bool> routine;
		while (!offSignal)
		{
			if (routine.Done())
			{
				try { routine.Rethrow(); }
				catch (...) { if (IDTLogger) IDTLogger->error("[UI3] 交互续接失败，取消当前手势"); }
				// Cancel 已清理旧接触；保留边界后已入队的新接触，交给新协程。
				context = {};
				session = std::make_unique<BarInteractionSession>(*this, mainButtonClickPulseSerial, memberAccess);
				routine = session->Run(); routine.Start();
			}
			const auto observed = sharedInputMailbox.Revision();
			auto deadline = PumpSharedInteraction(context, InteractionClock::now());
			if (routine.Done()) continue;
			// 无输入或手势计时期限时完全休眠；退出由同一 mailbox 显式唤醒。
			sharedInputMailbox.WaitUntil(deadline, observed);
		}
		CancelInteractionBusiness();
		context.cancelled = true;
		for (unsigned budget = 0; budget != 64 && !routine.Done(); ++budget)
			if (!ResumeMainInteraction(context, InteractionClock::now())) break;
		CancelAcceptedMainCapture();
		CancelRegisteredInteractionSurfaces();
	}
	interactionWorkerRunning.store(false, std::memory_order_release);
	for (const auto& registration : SnapshotInteractionRegistrations())
	{
		std::lock_guard lock(registration->callbackMutex);
		registration->callbackDrained.notify_all();
	}
	interactionWorkerThreadId.store(0, std::memory_order_release);
	mainInteractionAwaitContext = nullptr;
	pendingMainInput.clear();
	// 停止时排空已经接受的保存/确认任务，不让其越过业务服务的生命周期。
	DrainInteractionBusiness();
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
		if (RegisterRawInputDevices(&rawInputDevice, 1, sizeof(rawInputDevice)))
		{
			Inkeys::Drawing::Draw3::RecordCursorDiagnostic(
				"raw-registration stage=bar-remove known=1 active=0 hwnd=%p", static_cast<void*>(hWnd));
			return true;
		}

		DWORD removalError = GetLastError();
		Inkeys::Drawing::Draw3::RecordCursorDiagnostic(
			"raw-registration stage=bar-remove known=0 ok=0 error=%lu hwnd=%p",
			static_cast<unsigned long>(removalError), static_cast<void*>(hWnd));
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
		Inkeys::Drawing::Draw3::RecordCursorDiagnostic(
			"raw-registration stage=bar-add known=0 ok=0 error=%lu hwnd=%p",
			static_cast<unsigned long>(registrationError), static_cast<void*>(hWnd));
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
	Inkeys::Drawing::Draw3::RecordCursorDiagnostic(
		"raw-registration stage=bar-add known=1 active=1 hwnd=%p flags=0x%lx",
		static_cast<void*>(hWnd), static_cast<unsigned long>(rawInputDevice.dwFlags));
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
InteractionTask<BarSeekResult> BarUISetClass::Seek(const ExMessage& msg)
{
	using namespace Inkeys::UI::Bar;
	BarSeekResult result;
	if (IsBottomDockIndicatorPresentedAt(msg.x, msg.y))
	{
		result.allowClick = false;
		co_return result;
	}
	auto IsLeftButtonDown = []() -> bool
		{
			// HWND 移动后可能收不到抬手消息，拖动循环必须以系统实时按键态结束。
			return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		};
	const bool touchGesture = IsBarTouchPointerMessage(msg);
	if (touchGesture && IsBarTouchCancelMessage(msg))
	{
		result.allowClick = false;
		co_return result;
	}
	if ((touchGesture && (!msg.lbutton || IsBarTouchCancelMessage(msg)))
		|| (!touchGesture && !IsLeftButtonDown())) co_return result;

	auto mainButton = superellipseMap[BarUISetSuperellipseEnum::MainButton];
	if (!mainButton) co_return result;

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
		if (expected == BarDirectWindowDragPhase::Dragging) co_return result;
		co_await InteractionDelay(std::chrono::milliseconds(1));
		if (offSignal || InteractionCancelled()) co_return result;
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
			ResolveDockDpiScale(initialPresentedSnapshot.dpi) };
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
		co_return result;
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
	const double initialFloatingVisibleBottomScreenY =
		startPointer.y - grabOffsetScreenY + VisibleHalfHeightScreen();
		const double initialDockCenterScreenY = ResolveBarBottomDockCenterScreenY(
			ResolveBarBottomDockLine(
				environment.monitorBounds, environment.workArea,
				environment.insetDip, environment.dpiScale),
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
		const BarBottomDockCenterDragUpdate& centerUpdate)
		{
			DockPublication publication;
			publication.visualChanged = update.modeChanged || centerUpdate.modeChanged
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
				update.modeChanged || centerUpdate.modeChanged);
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
					// 显示环境变化只消费未形变坐标；视觉果冻不能回灌抓手和捕获阈值。
					baseMainCenterScreenX = presented.rawMainCenterScreenX
						- presented.directTranslation.x
						- presented.horizontalMapping.rigidGripTranslationXDip * presented.zoom;
					baseMainCenterScreenY = presented.mainCenterScreenY
						- presented.directTranslation.y;
					bodyToMainCenterScreenX = presented.rawBodyCenterScreenX
						- presented.rawMainCenterScreenX;
					grabOffsetScreenX = pointer.x - presented.rawMainCenterScreenX;
					grabOffsetScreenY = pointer.y
						- (presented.mainCenterScreenY
							+ presented.rigidTranslationDip * presented.zoom);
					const double floatingBottom = pointer.y - grabOffsetScreenY
						+ VisibleHalfHeightScreen();
						const double dockCenter = ResolveBarBottomDockCenterScreenY(
							ResolveBarBottomDockLine(
								environment.monitorBounds, environment.workArea,
								environment.insetDip, environment.dpiScale),
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
					interactionZoom = presented.zoom;
					maximumInteractionZoom = max(
						maximumInteractionZoom, interactionZoom);
						environment = BarBottomDockEnvironment{
							presented.monitorBounds,
							presented.workArea,
							interactionZoom,
							ResolveDockInsetDip(),
							ResolveDockDpiScale(presented.dpi) };
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
							environment.dpiScale), bodyHeightDip,
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
				dockUpdate, centerUpdate);
			if (anyModeChanged)
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

	if (touchGesture)
	{
		ExMessage touchMessage = msg;
		while (!offSignal && !InteractionCancelled() && !directMoveFailed)
		{
			if (!(co_await NextBarInteractionMessage(
				touchMessage, EM_MOUSE, floating_window, true)))
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
		while (!offSignal && !InteractionCancelled() && !directMoveFailed)
		{
			if (!IsLeftButtonDown()) break;
			if (!GetCursorPos(&point))
			{
				gestureCancelled = true;
				break;
			}
			(void)ApplyAbsolutePointer(point);
			co_await InteractionDelay(std::chrono::milliseconds(8));
		}
	}

	if (InteractionCancelled()) gestureCancelled = true;
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
	POINT releasePoint{};
	if (result.allowClick && ReadBarPointerPosition(&releasePoint)
		&& BarScreenToLayout(releasePoint)
		&& IsBottomDockIndicatorPresentedAt(releasePoint.x, releasePoint.y))
		result.allowClick = false;
	co_return result;
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
		if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		{
			ExMessage queued{}; queued.message = static_cast<USHORT>(message);
			queued.category = EM_KEY; queued.vkcode = static_cast<BYTE>(wParam);
			(void)EnqueueMainInteractionMessage(hwnd, queued);
			return { Inkeys::Message::Action::Discard, 0 };
		}
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
		const bool down = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK;
		const auto ticket = down ? AcquireBarNativeCapture(hwnd) : mainNativeCaptureOwnership.Current(hwnd);
		const bool accepted = EnqueueMainInteractionMessage(hwnd, queued, ticket);
		if (message == WM_LBUTTONUP || (down && !accepted)) ReleaseBarNativeCapture(hwnd, ticket);
		return { accepted ? Inkeys::Message::Action::Discard : Inkeys::Message::Action::Handled, 0 };
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
			return EnqueueMainInteractionMessage(floating_window, message);
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
		return EnqueueMainInteractionMessage(floating_window, message);
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
